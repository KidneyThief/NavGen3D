// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DAltMover.h"
#include "NavGen3DSubsystem.h"
#include "DrawDebugHelpers.h"
#include "NavGen3DSettings.h"

NavGen3D_DISABLE_OPTIMIZATION

ANavGen3DAltMover::ANavGen3DAltMover()
{
	PrimaryActorTick.bCanEverTick = true;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ANavGen3DAltMover::BeginPlay()
{
	Super::BeginPlay();

	SimPathSearchSpace = MakeShared<FPathSearchSpace>();
}

void ANavGen3DAltMover::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SimPathSearchSpace.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANavGen3DAltMover::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateMovement(DeltaTime);
	DebugDraw();
}

void ANavGen3DAltMover::SimReset()
{
	SimVelocity = FVector::ZeroVector;
	bSettled    = false;
}

void ANavGen3DAltMover::UpdateMovement(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	if (bSettled)
	{
		return;
	}

	if (!SimPathSearchSpace.IsValid())
	{
		return;
	}

	// While a search is in flight, wait for the worker thread to complete it.
	if (SimPathSearchSpace->Status == EPathSearchStatus::InProgress)
	{
		return;
	}

	const FVector TargetLocation = GetTargetLocation();
	if (TargetLocation.X >= FLT_MAX)
	{
		return;
	}

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	if (FVector::Dist(TargetLocation, SimPathSearchSpace->Destination) > AgentRadius)
	{
		SimPathSearchSpace->Initialize(Subsystem, AgentIndex, GetActorLocation(), TargetLocation);
		Subsystem->RegisterPathSearchSpace(SimPathSearchSpace);
	}
}

FVector ANavGen3DAltMover::GetTargetLocation() const
{
	UWorld* World = GetWorld();
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!World || !Subsystem)
	{
		return FVector(FLT_MAX);
	}

	FVector Location = FVector(FLT_MAX);
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			Location = Pawn->GetActorLocation();
		}
	}
	if (Location.X >= FLT_MAX)
	{
		return FVector(FLT_MAX);
	}

	// Find all geometry within PaddedRadius of the target location.
	// OverlapMultiByChannel returns one result per component (not per triangle), which
	// avoids the many-hits problem of SweepMultiByChannel on complex collision meshes.
	const float PaddedRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/true);
	const FCollisionShape PaddedSphere = FCollisionShape::MakeSphere(PaddedRadius);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), PaddedSphere);

	if (Overlaps.IsEmpty())
	{
		return Location;
	}

	// Compute MTD for each overlapping component. The contact point on the surface is
	// Location + MTD.Direction * (MTD.Distance - PaddedRadius).
	struct FPush { FVector Direction; float Distance; };
	TArray<FPush> Pushes;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (!Overlap.Component.IsValid())
		{
			continue;
		}
		if (Overlap.Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}
		FMTDResult MTD;
		if (Overlap.Component->ComputePenetration(MTD, PaddedSphere, Location, FQuat::Identity))
		{
			Pushes.Add({ MTD.Direction, MTD.Distance });
		}
	}

	// If any two push directions oppose each other the target is trapped — no valid location.
	for (int32 i = 0; i < Pushes.Num(); ++i)
	{
		for (int32 j = i + 1; j < Pushes.Num(); ++j)
		{
			if (FVector::DotProduct(Pushes[i].Direction, Pushes[j].Direction) < -0.5f)
			{
				return FVector(FLT_MAX);
			}
		}
	}

	// Push the target clear of each overlapping surface.
	for (const FPush& Push : Pushes)
	{
		Location += Push.Direction * (Push.Distance + 0.125f);
	}

	return Location;
}

void ANavGen3DAltMover::MoveWithSweep(const FVector& NewLocation)
{
	UWorld* World = GetWorld();
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!World || !Subsystem)
	{
		SetActorLocation(NewLocation);
		return;
	}

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FCollisionShape AgentSphere = FCollisionShape::MakeSphere(AgentRadius);

	// Depenetrate: resolve any existing geometry overlap before attempting the move.
	// Iterating handles cases where resolving one penetration nudges us into another.
	FVector SafeStart = GetActorLocation();
	{
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		for (int32 Iter = 0; Iter < 4; ++Iter)
		{
			TArray<FOverlapResult> Overlaps;
			if (!World->OverlapMultiByObjectType(Overlaps, SafeStart, FQuat::Identity,
				FCollisionObjectQueryParams(ECC_WorldStatic), AgentSphere, QueryParams))
			{
				break;
			}

			bool bResolvedAny = false;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (!Overlap.Component.IsValid())
				{
					continue;
				}
				if (Overlap.Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				{
					continue;
				}
				FMTDResult MTD;
				if (Overlap.Component->ComputePenetration(MTD, AgentSphere, SafeStart, FQuat::Identity))
				{
					SafeStart  += MTD.Direction * (MTD.Distance + 0.125f);
					bResolvedAny = true;
				}
			}

			if (!bResolvedAny)
			{
				break;
			}
		}
	}
	SetActorLocation(SafeStart);

	// Sweep from the depenetrated position to the desired destination.
	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByChannel(Hit, SafeStart, NewLocation, FQuat::Identity, ECC_WorldStatic, AgentSphere);

	if (!bBlocked)
	{
		SetActorLocation(NewLocation);
		return;
	}

	if (Hit.bStartPenetrating)
	{
		// Depenetration above should have cleared this; if it persists (e.g. geometry
		// with no valid MTD), stay at SafeStart rather than moving into geometry.
		return;
	}

	// Hit.Location is the safe impact point — guaranteed not to penetrate.
	SetActorLocation(Hit.Location);

	// Cancel only the velocity component heading into the surface.
	const float VelIntoSurface = FVector::DotProduct(SimVelocity, Hit.Normal);
	if (VelIntoSurface < 0.0f)
	{
		SimVelocity -= VelIntoSurface * Hit.Normal;
	}
}

void ANavGen3DAltMover::DebugDraw()
{
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float   Radius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FVector Center = GetActorLocation();
	const FColor  Color  = bSettled ? FColor::Green : FColor::Cyan;

	if (bDebugDrawAsArrow)
	{
		const FVector Forward    = GetActorForwardVector();
		const float   ArrowLength = Radius * 2.0f;
		const FVector Start      = Center - Forward * Radius;
		const FVector End        = Center + Forward * Radius;
		const FVector ShaftEnd   = Start + Forward * ArrowLength * 0.7f;
		DrawDebugCylinder(World, Start, ShaftEnd, ArrowLength * 0.05f, 8, Color, false, -1.0f, 0, 1.0f);
		const float HeadHalfAngle = FMath::Atan2(ArrowLength * 0.15f, ArrowLength * 0.3f);
		DrawDebugCone(World, End, -Forward, ArrowLength * 0.3f, HeadHalfAngle, HeadHalfAngle, 8, Color, false, -1.0f, 0, 1.0f);
	}
	else
	{
		DrawDebugSphere(World, Center, Radius, 12, Color, false, -1.0f);
	}

	if (Subsystem->DebugLevel >= 1)
	{
        const float DrawTime = World->GetDeltaSeconds() * 1.1f;
		if (SimPathSearchSpace.IsValid() && SimPathSearchSpace->Status == EPathSearchStatus::Success)
		{
			SimPathSearchSpace->DebugDrawPath(DrawTime);
		}

	}
}

NavGen3D_ENABLE_OPTIMIZATION
