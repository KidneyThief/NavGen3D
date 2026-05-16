// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DMover.h"
#include "NavGen3DSubsystem.h"
#include "DrawDebugHelpers.h"
#include "NavGen3DSettings.h"

// -- defined in NavGen3DSettings.h
NavGen3D_DISABLE_OPTIMIZATION

ANavGen3DMover::ANavGen3DMover()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ANavGen3DMover::BeginPlay()
{
	Super::BeginPlay();
}

void ANavGen3DMover::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateGoalLocation();
	UpdateSimLocation();
	DebugDrawMover();
}

void ANavGen3DMover::UpdateGoalLocation()
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

	const FVector CameraLocation = Subsystem->GetCameraLocation(AgentIndex);
	if (CameraLocation.X >= FLT_MAX)
	{
		GoalLocation = FVector(FLT_MAX);
		return;
	}

	// If settled, just verify the goal is still valid — no path work needed.
	if (bSettled && GoalLocation.X < FLT_MAX)
	{
		const float DistToCamera   = FVector::Distance(GoalLocation, CameraLocation);
		const bool  bWithinMax     = DistToCamera <= MaxRepositionDistance;
		const bool  bGoalBlocked   = bWithinMax ? World->LineTraceTestByChannel(GoalLocation, CameraLocation, ECC_Visibility) : true;
		if (bWithinMax && !bGoalBlocked)
		{
			return;
		}
		bSettled = false;
	}

	// Local search space — only used to find the goal point; discarded after.
	FPathSearchSpace GoalSearchSpace;
	GoalSearchSpace.Initialize(Subsystem, AgentIndex, GetActorLocation(), CameraLocation);
	if (!Subsystem->PathFind(GoalSearchSpace) || GoalSearchSpace.PathSolution.IsEmpty())
	{
		GoalLocation = FVector(FLT_MAX);
		return;
	}

	// If we already have a valid goal, keep it as long as it has LOS and hasn't gone beyond MaxRepositionDistance.
	if (GoalLocation.X < FLT_MAX)
	{
		const float DistToCamera = FVector::Distance(GoalLocation, CameraLocation);
		const bool bWithinMax   = DistToCamera <= MaxRepositionDistance;
		const bool bBlocked     = bWithinMax ? World->LineTraceTestByChannel(GoalLocation, CameraLocation, ECC_Visibility) : true;
		if (bWithinMax && !bBlocked)
		{
			SimPathSearchSpace.Initialize(Subsystem, AgentIndex, GetActorLocation(), GoalLocation);
			Subsystem->PathFind(SimPathSearchSpace);
			return;
		}
	}

	// Find a new goal — walk path segments from camera end toward mover at agentRadius increments.
	// Skip LOS checks until we're within the valid distance range.
	const FVector PreviousGoal = GoalLocation;
	GoalLocation = FVector(FLT_MAX);

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const TArray<FVector>& Solution = GoalSearchSpace.PathSolution;

	bool bDone = false;
	for (int32 i = Solution.Num() - 1; i > 0 && !bDone; --i)
	{
		const FVector SegEnd   = Solution[i];
		const FVector SegDelta = Solution[i - 1] - SegEnd;
		const float   SegLen   = SegDelta.Size();
		if (SegLen < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector SegDir = SegDelta / SegLen;
		for (float t = 0.0f; t <= SegLen && !bDone; t += AgentRadius)
		{
			const FVector Candidate    = SegEnd + SegDir * t;
			const float   DistToCamera = FVector::Distance(Candidate, CameraLocation);

			if (DistToCamera > MinApproachDistance)
			{
				bDone = true;
				break;
			}

			const bool bBlocked = World->LineTraceTestByChannel(Candidate, CameraLocation, ECC_Visibility);
			if (!bBlocked)
			{
				GoalLocation = Candidate;
				bDone = true;
			}
		}
	}

	const bool bNewGoal = GoalLocation.X < FLT_MAX;

	// No suitable candidate found — keep the previous goal rather than going invalid.
	if (!bNewGoal && PreviousGoal.X < FLT_MAX)
	{
		GoalLocation = PreviousGoal;
	}

	if (GoalLocation.X < FLT_MAX)
	{
		SimPathSearchSpace.Initialize(Subsystem, AgentIndex, GetActorLocation(), GoalLocation);
		Subsystem->PathFind(SimPathSearchSpace);
		if (bNewGoal)
		{
			SimReset();
		}
	}
}

bool ANavGen3DMover::IsAtGoalLocation() const
{
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (GoalLocation.X >= FLT_MAX)
	{
		return false;
	}

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	if (FVector::Distance(GetActorLocation(), GoalLocation) > AgentRadius)
	{
		return false;
	}

	const FVector CameraLocation = Subsystem->GetCameraLocation(AgentIndex);
	const bool bBlocked = World->LineTraceTestByChannel(GetActorLocation(), CameraLocation, ECC_Visibility);
	return !bBlocked;
}

void ANavGen3DMover::SimReset(bool bResetPhysics)
{
	SimPathNodeIndex = 0;
	bSettled         = false;
	bFacingPath      = true;
	bDecelerating    = false;
	if (bResetPhysics)
	{
		SimVelocity = FVector::ZeroVector;
	}
}

void ANavGen3DMover::UpdateSimPathNode()
{
	const TArray<FVector>& Path = SimPathSearchSpace.PathSolution;
	if (Path.IsEmpty())
	{
		return;
	}

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

	SimPathNodeIndex = FMath::Clamp(SimPathNodeIndex, 0, Path.Num() - 1);

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(AgentRadius);
	const FVector ActorLocation = GetActorLocation();

	if (SimPathNodeIndex < Path.Num() - 1)
	{
		const bool bBlocked = World->SweepTestByChannel(ActorLocation, Path[SimPathNodeIndex + 1], FQuat::Identity, ECC_WorldStatic, Sphere);
		if (!bBlocked)
		{
			++SimPathNodeIndex;
		}
	}
	else if (SimPathNodeIndex > 0)
	{
		const bool bBlocked = World->SweepTestByChannel(ActorLocation, Path[SimPathNodeIndex], FQuat::Identity, ECC_WorldStatic, Sphere);
		if (bBlocked)
		{
			--SimPathNodeIndex;
		}
	}
}

bool ANavGen3DMover::UpdateSimPathFollowing()
{
	UpdateSimPathNode();

	const TArray<FVector>& Path = SimPathSearchSpace.PathSolution;
	if (Path.IsEmpty() || SimPathNodeIndex < 0 || SimPathNodeIndex >= Path.Num())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float DeltaTime        = World->GetDeltaSeconds();
	const FVector NodeLocation   = Path[SimPathNodeIndex];
	const FVector ToNode         = (NodeLocation - GetActorLocation()).GetSafeNormal();
	const float   CurrentSpeed   = SimVelocity.Size();

	// Commit to deceleration once within stopping distance, but only when velocity is
	// negligible or already directed toward the destination — so the steering block gets
	// a chance to correct sideways/backward momentum before we begin braking.
	const bool bAtFinalNode = SimPathNodeIndex == Path.Num() - 1;
	if (!bDecelerating && bAtFinalNode && SimAcceleration > KINDA_SMALL_NUMBER)
	{
		const bool bVelocityNegligible = SimVelocity.IsNearlyZero();
		const bool bHeadingToward      = !bVelocityNegligible && FVector::DotProduct(SimVelocity.GetSafeNormal(), ToNode) > 0.0f;
		if (bVelocityNegligible || bHeadingToward)
		{
			const float StoppingDistance = (CurrentSpeed * CurrentSpeed) / (2.0f * SimAcceleration);
			if (FVector::Distance(GetActorLocation(), NodeLocation) <= StoppingDistance)
			{
				bDecelerating = true;
			}
		}
	}

	if (bDecelerating)
	{
		const float NewSpeed = FMath::Max(0.0f, CurrentSpeed - SimAcceleration * DeltaTime);
		SimVelocity = NewSpeed > KINDA_SMALL_NUMBER ? SimVelocity.GetSafeNormal() * NewSpeed : FVector::ZeroVector;
		SetActorLocation(GetActorLocation() + SimVelocity * DeltaTime, /*bSweep=*/true);
		return true;
	}

	// Build a steering direction: toward the node, plus a counter for any velocity that is
	// perpendicular or away from the node (forward-toward velocity is left uncontested).
	FVector SteeringDirection = ToNode;
	if (!SimVelocity.IsNearlyZero())
	{
		const float ForwardSpeed      = FVector::DotProduct(SimVelocity, ToNode);
		const FVector TowardComponent = FMath::Max(0.0f, ForwardSpeed) * ToNode;
		SteeringDirection             = ToNode - (SimVelocity - TowardComponent);
	}

	const FVector AccelerationVector = SteeringDirection.GetSafeNormal() * SimAcceleration * DeltaTime;
	SimVelocity = (SimVelocity + AccelerationVector).GetClampedToMaxSize(SimMaxVelocity);
	SetActorLocation(GetActorLocation() + SimVelocity * DeltaTime, /*bSweep=*/true);

	return true;
}

void ANavGen3DMover::UpdateSimRotation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	const float   DeltaTime      = World->GetDeltaSeconds();
	const FVector ActorLocation  = GetActorLocation();
	const FVector CameraLocation = Subsystem->GetCameraLocation(AgentIndex);
	const bool    bCameraValid   = CameraLocation.X < FLT_MAX;

	// Decide camera vs path look target.
	// Settled and decel-phase always force camera. Otherwise use hysteresis on the
	// SimPathingAngle: switch path→camera below 0.8×, switch camera→path above 1.2×.
	bool bLookAtCamera = false;
	if (bSettled || bDecelerating)
	{
		bLookAtCamera = true;
		bFacingPath   = false;
	}
	else if (bCameraValid)
	{
		const FVector ToCamera   = CameraLocation - ActorLocation;
		const float   CameraDist = ToCamera.Size();
		if (CameraDist > KINDA_SMALL_NUMBER)
		{
			const FVector ToCameraDir   = ToCamera / CameraDist;
			const float   AngleToCamera = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(FVector::DotProduct(GetActorForwardVector(), ToCameraDir), -1.0f, 1.0f)));
			const bool bHasLOS = !World->LineTraceTestByChannel(ActorLocation, CameraLocation, ECC_Visibility);

			if (bHasLOS)
			{
				bLookAtCamera = bFacingPath ? (AngleToCamera < SimPathingAngle * 0.8f)
				                            : (AngleToCamera < SimPathingAngle * 1.2f);
			}
		}
		bFacingPath = !bLookAtCamera;
	}
	else
	{
		bFacingPath = true;
	}

	// Compute rotation target.
	FRotator TargetRotation;
	if (bLookAtCamera && bCameraValid)
	{
		const FVector ToCamera = (CameraLocation - ActorLocation).GetSafeNormal();
		if (ToCamera.IsNearlyZero())
		{
			return;
		}
		TargetRotation = ToCamera.Rotation();
	}
	else
	{
		// Look ahead along the path at 2× agent radius; fall back to goal direction.
		const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
		FVector LookAheadLocation;
		if (SimPathLookAheadLocation(ActorLocation, AgentRadius * 2.0f, LookAheadLocation))
		{
			const FVector ToLookAhead = (LookAheadLocation - ActorLocation).GetSafeNormal();
			if (ToLookAhead.IsNearlyZero())
			{
				return;
			}
			TargetRotation = ToLookAhead.Rotation();
		}
		else if (GoalLocation.X < FLT_MAX)
		{
			const FVector ToGoal = (GoalLocation - ActorLocation).GetSafeNormal();
			if (ToGoal.IsNearlyZero())
			{
				return;
			}
			TargetRotation = ToGoal.Rotation();
		}
		else
		{
			return;
		}
	}

	const FRotator NewRotation = FMath::RInterpConstantTo(GetActorRotation(), TargetRotation, DeltaTime, SimTurnRate);
	SetActorRotation(NewRotation);
}

void ANavGen3DMover::UpdateSimLocation()
{
	if (GoalLocation.X >= FLT_MAX)
	{
		return;
	}

	UpdateSimRotation();

	if (bSettled)
	{
		return;
	}

	if (IsAtGoalLocation() && SimVelocity.IsNearlyZero())
	{
		SimVelocity = FVector::ZeroVector;
		bSettled    = true;
		return;
	}

	// Gate on velocity direction only — visual facing is independent of movement.
	// When velocity is zero or already pointed toward the goal, path following runs
	// freely and its steering builds velocity toward the next node. Only decelerate
	// when existing velocity is misaligned enough that we'd be accelerating backward.
	if (!SimVelocity.IsNearlyZero())
	{
		const FVector ToGoal   = (GoalLocation - GetActorLocation()).GetSafeNormal();
		const float   CosAngle = FMath::Cos(FMath::DegreesToRadians(SimPathingAngle));
		if (FVector::DotProduct(SimVelocity.GetSafeNormal(), ToGoal) < CosAngle)
		{
			UWorld* World = GetWorld();
			if (World)
			{
				const float DeltaTime    = World->GetDeltaSeconds();
				const float CurrentSpeed = SimVelocity.Size();
				const float NewSpeed     = FMath::Max(0.0f, CurrentSpeed - SimAcceleration * DeltaTime);
				SimVelocity = NewSpeed > KINDA_SMALL_NUMBER ? SimVelocity.GetSafeNormal() * NewSpeed : FVector::ZeroVector;
				SetActorLocation(GetActorLocation() + SimVelocity * DeltaTime, /*bSweep=*/true);
			}
			return;
		}
	}

	UpdateSimPathFollowing();
}

bool ANavGen3DMover::SimPathLookAheadLocation(const FVector& InCurrentLocation, float InDistance, FVector& OutPathLocation) const
{
	const TArray<FVector>& Path = SimPathSearchSpace.PathSolution;
	if (Path.IsEmpty())
	{
		return false;
	}

	float RemainingDistance = InDistance;
	int32 NextSegmentIndex; // first segment index to walk after finding the start point

	if (SimPathNodeIndex == 0 || Path.Num() == 1)
	{
		// No previous node — back out dist-to-origin from InDistance, then walk from Path[0].
		RemainingDistance = FMath::Max(0.0f, InDistance - FVector::Distance(InCurrentLocation, Path[0]));
		NextSegmentIndex  = 0;
	}
	else
	{
		// Project current location onto segment [SimPathNodeIndex-1, SimPathNodeIndex].
		const FVector SegA     = Path[SimPathNodeIndex - 1];
		const FVector SegB     = Path[SimPathNodeIndex];
		const FVector SegDelta = SegB - SegA;
		const float   SegLenSq = SegDelta.SizeSquared();

		FVector Projected;
		if (SegLenSq < KINDA_SMALL_NUMBER)
		{
			Projected = SegA;
		}
		else
		{
			const float t = FMath::Clamp(FVector::DotProduct(InCurrentLocation - SegA, SegDelta) / SegLenSq, 0.0f, 1.0f);
			Projected = SegA + SegDelta * t;
		}

		const float DistToSegEnd = FVector::Distance(Projected, SegB);
		if (RemainingDistance <= DistToSegEnd)
		{
			OutPathLocation = Projected + (SegB - Projected).GetSafeNormal() * RemainingDistance;
			return true;
		}

		RemainingDistance -= DistToSegEnd;
		NextSegmentIndex   = SimPathNodeIndex; // continue from SegB = Path[SimPathNodeIndex]
	}

	// Walk forward through remaining segments.
	for (int32 i = NextSegmentIndex; i < Path.Num() - 1; ++i)
	{
		const FVector SegA   = Path[i];
		const FVector SegB   = Path[i + 1];
		const float   SegLen = FVector::Distance(SegA, SegB);

		if (SegLen < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		if (RemainingDistance <= SegLen)
		{
			OutPathLocation = SegA + (SegB - SegA).GetSafeNormal() * RemainingDistance;
			return true;
		}

		RemainingDistance -= SegLen;
	}

	// Distance exceeded the full path — clamp to destination.
	OutPathLocation = Path.Last();
	return true;
}

void ANavGen3DMover::DebugDrawSimulation()
{
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem || Subsystem->DebugLevel < 2)
	{
		return;
	}

	const UWorld* World    = GetWorld();
	const float   DrawTime = World ? World->GetDeltaSeconds() * 1.1f : 0.0f;
	SimPathSearchSpace.DebugDrawPath(DrawTime);

	if (World && GoalLocation.X < FLT_MAX)
	{
		const float Radius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
		DrawDebugSphere(World, GoalLocation, Radius, 12, FColor::Orange, /*bPersistentLines=*/false, DrawTime);
	}
}

void ANavGen3DMover::DebugDrawMover()
{
	UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Radius      = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const float ArrowLength = Radius * 2.0f;
	const float Thickness   = 1.0f;
	const FVector Forward   = GetActorForwardVector();
	const FVector Start     = GetActorLocation();
	const FVector End       = Start + Forward * ArrowLength;
	const FColor  DrawColor = IsAtGoalLocation() ? FColor::Green : FColor::Orange;

	// Shaft: 70% of length, thin cylinder
	const float ShaftLength  = ArrowLength * 0.7f;
	const FVector ShaftEnd   = Start + Forward * ShaftLength;
	const float ShaftRadius  = ArrowLength * 0.05f;
	DrawDebugCylinder(World, Start, ShaftEnd, ShaftRadius, 8, DrawColor, /*bPersistentLines=*/false, /*LifeTime=*/-1.0f, /*DepthPriority=*/0, Thickness);

	// Head: cone with apex at tip, opening backward over the remaining 30%
	const float HeadLength    = ArrowLength * 0.3f;
	const float HeadHalfAngle = FMath::Atan2(ArrowLength * 0.15f, HeadLength);
	DrawDebugCone(World, End, -Forward, HeadLength, HeadHalfAngle, HeadHalfAngle, 8, DrawColor, /*bPersistentLines=*/false, /*LifeTime=*/-1.0f, /*DepthPriority=*/0, Thickness);

	DebugDrawSimulation();
}

NavGen3D_ENABLE_OPTIMIZATION
