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
	SimUpdateMovement(DeltaTime);
	DebugDraw();
}

void ANavGen3DAltMover::SimReset(bool bResetPhysics)
{
	if (bResetPhysics)
	{
		SimVelocity = FVector::ZeroVector;
	}

	SimPathVector             = FVector::ZeroVector;
	SimApproachVector         = FVector::ZeroVector;
	SimApproachTargetLocation = FVector(FLT_MAX);
	SimApproachTimer          = 0.0f;
	SimPathIndex              = 0;
	SimLookaheadLocation      = FVector(FLT_MAX);
	bIsApproachingTarget      = false;

	if (SimPathSearchSpace.IsValid())
	{
		SimPathSearchSpace->Reset();
	}
}

void ANavGen3DAltMover::SimUpdateMovement(float DeltaTime)
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

	if (!SimPathSearchSpace.IsValid())
	{
		return;
	}

	// While a search is in flight, wait for the worker thread to complete it.
	if (SimPathSearchSpace->Status == EPathSearchStatus::InProgress)
	{
		return;
	}

	SimApproachTimer = FMath::Max(0.0f, SimApproachTimer - DeltaTime);

	auto DecelerateAndMove = [&]()
	{
		if (!SimVelocity.IsNearlyZero())
		{
			const float CurrentSpeed = SimVelocity.Size();
			const float NewSpeed     = FMath::Max(0.0f, CurrentSpeed - SimAcceleration * DeltaTime);
			SimVelocity = NewSpeed > KINDA_SMALL_NUMBER ? SimVelocity.GetSafeNormal() * NewSpeed : FVector::ZeroVector;
			MoveWithSweep(GetActorLocation() + SimVelocity * DeltaTime);
		}
	};

	const FVector TargetLocation = GetTargetLocation();
	if (TargetLocation.X >= FLT_MAX)
	{
		DecelerateAndMove();
		return;
	}

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);

	const bool bApproaching = IsApproachingTarget();

	if (!bApproaching && FVector::Dist(TargetLocation, SimPathSearchSpace->Destination) > AgentRadius)
	{
		SimPathSearchSpace->Initialize(Subsystem, AgentIndex, GetActorLocation(), TargetLocation);
		Subsystem->RegisterPathSearchSpace(SimPathSearchSpace);
		DecelerateAndMove();
		return;
	}

	if (!bApproaching && SimPathSearchSpace->Status != EPathSearchStatus::Success)
	{
		DecelerateAndMove();
		return;
	}

	if (bApproaching)
	{
		UpdateSimApproachTarget(DeltaTime);
		const float BrakingAccel = SimAcceleration * SimBrakingMultiplier;
		SimVelocity = (SimVelocity + SimApproachVector * BrakingAccel * DeltaTime).GetClampedToMaxSize(SimMaxVelocity);
	}
	else
	{
		UpdateSimPathFollowing(DeltaTime);
		SimVelocity = (SimVelocity + SimPathVector * SimAcceleration * DeltaTime).GetClampedToMaxSize(SimMaxVelocity);
	}

	MoveWithSweep(GetActorLocation() + SimVelocity * DeltaTime);
	UpdateSimRotation(DeltaTime);
}

void ANavGen3DAltMover::UpdateSimApproachTarget(float DeltaTime)
{
	SimApproachVector = FVector::ZeroVector;

	if (SimApproachTargetLocation.X >= FLT_MAX)
	{
		return;
	}

	const FVector TargetLocation = SimApproachTargetLocation;

	const FVector ToTarget      = TargetLocation - GetActorLocation();
	const float   DistToTarget  = ToTarget.Size();
	const FVector ToTargetDir   = DistToTarget > KINDA_SMALL_NUMBER ? ToTarget / DistToTarget : FVector::ZeroVector;

	if (ToTargetDir.IsNearlyZero())
	{
		return;
	}

	// Desired stop point is at ApproachDistMin. Compute the remaining braking distance.
	const float DistToStop = DistToTarget - ApproachDistMin;

	// Ideal speed at this distance: sqrt(2*a*d) scaled by SimBrakingMultiplier so we
	// commit to stopping earlier than pure SimAcceleration braking would require.
	const float BrakingAccel  = SimAcceleration * SimBrakingMultiplier;
	const float IdealSpeed    = DistToStop > 0.0f ? FMath::Sqrt(2.0f * BrakingAccel * DistToStop) : 0.0f;
	const FVector DesiredVelocity = ToTargetDir * FMath::Min(IdealSpeed, SimMaxVelocity);

	// The seek vector counters both lateral drift (which would lose LOS) and
	// any excess speed beyond the braking profile.
	SimApproachVector = (DesiredVelocity - SimVelocity).GetSafeNormal();
}

void ANavGen3DAltMover::UpdateSimPathNode()
{
	if (!SimPathSearchSpace.IsValid() || SimPathSearchSpace->Status != EPathSearchStatus::Success)
	{
		return;
	}

	const TArray<FVector>& Path = SimPathSearchSpace->PathSolution;
	if (Path.IsEmpty())
	{
		return;
	}

	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	SimPathIndex = FMath::Clamp(SimPathIndex, 0, Path.Num() - 1);

	const int32   DestIndex     = Path.Num() - 1;
	const FVector ActorLocation = GetActorLocation();

	auto HasLOS = [&](const FVector& NodeLocation) -> bool
	{
		return Subsystem->SweepAgentLOS(AgentIndex, ActorLocation, NodeLocation, this);
	};

	if (SimPathIndex != DestIndex)
	{
		if (HasLOS(Path[SimPathIndex + 1]))
		{
			++SimPathIndex;
		}
		else if (!HasLOS(Path[SimPathIndex]))
		{
			bool bFoundFallback = false;
			for (int32 i = SimPathIndex - 1; i >= 0; --i)
			{
				if (HasLOS(Path[i]))
				{
					SimPathIndex    = i;
					bFoundFallback  = true;
					break;
				}
			}

			if (!bFoundFallback)
			{
				SimReset(false);
			}
		}
	}
}

void ANavGen3DAltMover::UpdateSimPathFollowing(float DeltaTime)
{
	UpdateSimPathNode();

	if (!SimPathSearchSpace.IsValid() || SimPathSearchSpace->Status != EPathSearchStatus::Success)
	{
		return;
	}

	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);

	const TArray<FVector>& Path = SimPathSearchSpace->PathSolution;
	if (!Path.IsValidIndex(SimPathIndex))
	{
		return;
	}

	if (Subsystem->DebugLevel >= 2)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			const float DrawTime = World->GetDeltaSeconds() * 1.1f;
			DrawDebugDirectionalArrow(World, GetActorLocation(), Path[SimPathIndex], 20.0f, FColor::Yellow, false, DrawTime, 0, 2.0f);
		}
	}

	const FVector NodeLocation  = Path[SimPathIndex];
	const FVector ToNode        = (NodeLocation - GetActorLocation()).GetSafeNormal();
	const float   DistToNode    = FVector::Dist(GetActorLocation(), NodeLocation);
	const float   CurrentSpeed  = SimVelocity.Size();

	// Predictive corner braking: determine the target speed we need to arrive at the
	// current node, then check whether we need to start braking now to reach it.
	float TargetSpeed    = SimMaxVelocity;
	float BrakingDistance = 0.0f;

	const int32 DestIndex = Path.Num() - 1;
	if (SimPathIndex < DestIndex)
	{
		const FVector IncomingDir = ToNode;
		const FVector OutgoingDir = (Path[SimPathIndex + 1] - NodeLocation).GetSafeNormal();
		const float   CornerDot   = FVector::DotProduct(IncomingDir, OutgoingDir);
		const float   CornerSpeed = SimMaxVelocity * FMath::Max(0.0f, CornerDot);

		const float SpeedDeltaSq = CurrentSpeed * CurrentSpeed - CornerSpeed * CornerSpeed;
		BrakingDistance = SpeedDeltaSq > 0.0f ? SpeedDeltaSq / (2.0f * SimAcceleration) : 0.0f;

		if (DistToNode <= BrakingDistance)
		{
			TargetSpeed = FMath::Sqrt(CornerSpeed * CornerSpeed + 2.0f * SimAcceleration * DistToNode);
		}
	}

	const FVector DesiredVelocity = ToNode * FMath::Min(TargetSpeed, SimMaxVelocity);
	SimPathVector = (DesiredVelocity - SimVelocity).GetSafeNormal();

	const float LookaheadDist = FMath::Max(BrakingDistance, AgentRadius * 2.0f);
	PathLookAhead(Path, GetActorLocation(), LookaheadDist, SimLookaheadLocation);
}

bool ANavGen3DAltMover::PathLookAhead(const TArray<FVector>& InPath, const FVector& InLocation, float InDistance, FVector& OutLocation)
{
	if (InPath.IsEmpty())
	{
		return false;
	}

	// Find the nearest point on the path by projecting onto each segment.
	FVector NearestPoint  = InPath[0];
	int32   NearestSeg    = 0;
	float   NearestDistSq = FVector::DistSquared(InLocation, InPath[0]);

	for (int32 i = 0; i + 1 < InPath.Num(); ++i)
	{
		const FVector AB      = InPath[i + 1] - InPath[i];
		const float   ABLenSq = AB.SizeSquared();
		if (ABLenSq < KINDA_SMALL_NUMBER) continue;

		const float   t    = FMath::Clamp(FVector::DotProduct(InLocation - InPath[i], AB) / ABLenSq, 0.0f, 1.0f);
		const FVector Proj = InPath[i] + AB * t;
		const float   DistSq = FVector::DistSquared(InLocation, Proj);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestPoint  = Proj;
			NearestSeg    = i;
		}
	}

	// Walk InDistance forward along the path from NearestPoint.
	float Remaining = InDistance;

	if (NearestSeg + 1 < InPath.Num())
	{
		const float DistToSegEnd = FVector::Distance(NearestPoint, InPath[NearestSeg + 1]);
		if (Remaining <= DistToSegEnd)
		{
			OutLocation = NearestPoint + (InPath[NearestSeg + 1] - NearestPoint).GetSafeNormal() * Remaining;
			return true;
		}
		Remaining -= DistToSegEnd;
	}

	for (int32 i = NearestSeg + 1; i + 1 < InPath.Num(); ++i)
	{
		const float SegLen = FVector::Distance(InPath[i], InPath[i + 1]);
		if (SegLen < KINDA_SMALL_NUMBER) continue;
		if (Remaining <= SegLen)
		{
			OutLocation = InPath[i] + (InPath[i + 1] - InPath[i]).GetSafeNormal() * Remaining;
			return true;
		}
		Remaining -= SegLen;
	}

	OutLocation = InPath.Last();
	return true;
}

void ANavGen3DAltMover::UpdateSimRotation(float DeltaTime)
{
	FVector LookTarget = FVector(FLT_MAX);

	if (bIsApproachingTarget && SimApproachTargetLocation.X < FLT_MAX)
	{
		LookTarget = SimApproachTargetLocation;
	}
	else if (SimLookaheadLocation.X < FLT_MAX)
	{
		LookTarget = SimLookaheadLocation;
	}

	if (LookTarget.X >= FLT_MAX)
	{
		return;
	}

	const FVector ToTarget = (LookTarget - GetActorLocation()).GetSafeNormal();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	const FRotator TargetRot = ToTarget.Rotation();
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), TargetRot, DeltaTime, SimTurnRate));
}

bool ANavGen3DAltMover::IsApproachingTarget()
{
	const FVector TargetLocation = GetTargetLocation();
	if (TargetLocation.X >= FLT_MAX)
	{
		return bIsApproachingTarget;
	}

	UWorld* World = GetWorld();
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!World || !Subsystem)
	{
		return bIsApproachingTarget;
	}

	const float Dist = FVector::Dist(GetActorLocation(), TargetLocation);

	auto HasLOS = [&]() -> bool
	{
		return Subsystem->SweepAgentLOS(AgentIndex, GetActorLocation(), TargetLocation, this);
	};

	if (!bIsApproachingTarget)
	{
		if (Dist >= ApproachDistMin * 1.15f && Dist <= ApproachDistMax * 0.85f)
		{
			if (HasLOS())
			{
				bIsApproachingTarget      = true;
				SimApproachTimer          = 2.0f;
				SimApproachTargetLocation = TargetLocation;
			}
		}
	}
	else
	{
		if (SimApproachTimer <= 0.0f)
		{
			if (Dist < ApproachDistMin * 0.85f || Dist > ApproachDistMax * 1.15f)
			{
				bIsApproachingTarget = false;
			}
			else if (!HasLOS())
			{
				bIsApproachingTarget = false;
			}
		}
	}

	return bIsApproachingTarget;
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
				const ECollisionEnabled::Type CT = Overlap.Component->GetCollisionEnabled();
				if (CT != ECollisionEnabled::QueryAndPhysics && CT != ECollisionEnabled::PhysicsOnly)
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
	// Use multi + filter so query-only volumes (triggers, overlap zones) don't block movement.
	FCollisionQueryParams SweepParams;
	SweepParams.AddIgnoredActor(this);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, SafeStart, NewLocation, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), AgentSphere, SweepParams);

	FHitResult Hit;
	bool bBlocked = false;
	for (FHitResult& Candidate : Hits)
	{
		if (!Candidate.Component.IsValid()) continue;
		const ECollisionEnabled::Type CT = Candidate.Component->GetCollisionEnabled();
		if (CT != ECollisionEnabled::QueryAndPhysics && CT != ECollisionEnabled::PhysicsOnly) continue;
		Hit      = Candidate;
		bBlocked = true;
		break;
	}

	if (!bBlocked)
	{
		SetActorLocation(NewLocation);
		return;
	}

	if (Hit.bStartPenetrating)
	{
		// ImpactPoint is unreliable when start-penetrating; use the MTD normal instead —
		// it points away from the blocking geometry, the same role DeflectDir plays below.
		SetActorLocation(SafeStart);
		const FVector DeflectDir = Hit.Normal.GetSafeNormal();
		if (!DeflectDir.IsNearlyZero())
		{
			SimVelocity = DeflectDir * FMath::Max(0.0f, FVector::DotProduct(SimVelocity, DeflectDir));
		}
		else
		{
			SimVelocity = FVector::ZeroVector;
		}
		return;
	}

	// Hit.Location is the safe impact point — guaranteed not to penetrate.
	SetActorLocation(Hit.Location);

	// Deflect velocity along the direction from the impact point toward the intended
	// destination — this is the unblocked component of the move, not the surface normal.
	const FVector DeflectDir = (NewLocation - Hit.ImpactPoint).GetSafeNormal();
	if (!DeflectDir.IsNearlyZero())
	{
		SimVelocity = DeflectDir * FMath::Max(0.0f, FVector::DotProduct(SimVelocity, DeflectDir));
	}
	else
	{
		SimVelocity = FVector::ZeroVector;
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
	const FColor  Color  = bIsApproachingTarget ? FColor::Green : FColor::Orange;

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
