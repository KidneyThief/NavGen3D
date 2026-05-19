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
	UpdateApproachLocation();
	UpdateSimLocation();
	UpdateSimSteering();
	DebugDrawMover();
}

void ANavGen3DMover::UpdateApproachLocation()
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

	// Prefer player pawn; fall back to editor camera.
	FVector TargetLocation = FVector(FLT_MAX);
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			TargetLocation = Pawn->GetActorLocation();
		}
	}
	if (TargetLocation.X >= FLT_MAX)
	{
		TargetLocation = Subsystem->GetCameraLocation(AgentIndex);
	}
	if (TargetLocation.X >= FLT_MAX)
	{
		ApproachLocation = FVector(FLT_MAX);
		return;
	}

	// Lift the target to a minimum of 2x padded agent radius above the ground.
	{
		const float LiftRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/true);
		FHitResult Hit;
		const FVector TraceStart = TargetLocation + FVector(0.0f, 0.0f, LiftRadius);
		const FVector TraceEnd   = TargetLocation - FVector(0.0f, 0.0f, LiftRadius * 20.0f);
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic))
		{
			const float MinZ = Hit.ImpactPoint.Z + LiftRadius * 2.0f;
			if (TargetLocation.Z < MinZ)
			{
				TargetLocation.Z = MinZ;
			}
		}
	}

	const float DistToTarget = FVector::Distance(GetActorLocation(), TargetLocation);

	// Determine whether the current ApproachLocation is still valid.
	// bTooFar checks the approach point's own distance to the target (not the mover's) so that a
	// valid approach isn't discarded just because the mover is still far away en route to it.
	// bTooClose checks the mover's distance so we reset if the mover has overshot the approach zone.
	bool bNeedNewApproach = (ApproachLocation.X >= FLT_MAX);
	if (!bNeedNewApproach)
	{
		const float ApproachDistToTarget = FVector::Distance(ApproachLocation, TargetLocation);
		const bool bTooFar   = ApproachDistToTarget > ApproachDistance * 1.2f;
		const bool bTooClose = DistToTarget < ApproachDistance * 0.8f;
		const bool bLOSLost  = World->LineTraceTestByChannel(ApproachLocation, TargetLocation, ECC_Visibility);
		bNeedNewApproach = bTooFar || bTooClose || bLOSLost;
	}

	if (!bNeedNewApproach)
	{
		// Approach is still valid — keep the path toward the target current so the mover
		// continues along the correct route and the decel intercept fires at ApproachLocation.
		if (!bSettled)
		{
			SimPathSearchSpace.Initialize(Subsystem, AgentIndex, GetActorLocation(), TargetLocation);
			Subsystem->PathFind(SimPathSearchSpace);
		}
		return;
	}

	bSettled = false;

	// Pathfind to TargetLocation to evaluate a new approach point.
	FPathSearchSpace PathToTarget;
	PathToTarget.Initialize(Subsystem, AgentIndex, GetActorLocation(), TargetLocation);
	if (!Subsystem->PathFind(PathToTarget) || PathToTarget.PathSolution.IsEmpty())
	{
		ApproachLocation = FVector(FLT_MAX);
		SimPathSearchSpace = PathToTarget;
		return;
	}

	// Calculate stopping distance from current speed: d = v² / (2a).
	// Clamp to at least AgentRadius so the lookahead is never degenerate.
	const float CurrentSpeed  = SimVelocity.Size();
	const float DecelDist     = SimAcceleration > KINDA_SMALL_NUMBER
	                            ? (CurrentSpeed * CurrentSpeed) / (2.0f * SimAcceleration)
	                            : 0.0f;
	const float AgentRadius   = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const float LookaheadDist = FMath::Max(DecelDist, AgentRadius);

	// Walk the path forward by LookaheadDist from the mover's position.
	FVector LookaheadPoint = PathToTarget.PathSolution.Last();
	{
		const TArray<FVector>& Sol = PathToTarget.PathSolution;
		float Remaining = LookaheadDist;
		for (int32 i = 0; i + 1 < Sol.Num(); ++i)
		{
			const FVector& A   = Sol[i];
			const FVector& B   = Sol[i + 1];
			const float    Len = FVector::Distance(A, B);
			if (Len < KINDA_SMALL_NUMBER) { continue; }
			if (Remaining <= Len)
			{
				LookaheadPoint = A + (B - A).GetSafeNormal() * Remaining;
				break;
			}
			Remaining -= Len;
		}
	}

	const FVector PreviousApproach = ApproachLocation;

	// If the lookahead point has LOS to the target, commit to it as the approach.
	// Either way, SimPathSearchSpace follows the path to TargetLocation — ApproachLocation is
	// a stopping intercept along that path, not a separate navigation destination.
	const bool bHasLOS = !World->LineTraceTestByChannel(LookaheadPoint, TargetLocation, ECC_Visibility);
	ApproachLocation   = bHasLOS ? LookaheadPoint : FVector(FLT_MAX);
	SimPathSearchSpace = PathToTarget;

	const bool bApproachChanged = (PreviousApproach.X >= FLT_MAX) != (ApproachLocation.X >= FLT_MAX)
	    || (ApproachLocation.X < FLT_MAX && FVector::DistSquared(ApproachLocation, PreviousApproach) > 1.0f);
	if (bApproachChanged)
	{
		SimReset();
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

	if (ApproachLocation.X >= FLT_MAX)
	{
		return false;
	}

	const float AgentRadius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	if (FVector::Distance(GetActorLocation(), ApproachLocation) > AgentRadius)
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
		SimVelocity       = FVector::ZeroVector;
		SimSteeringUp     = FVector::ZeroVector;
		SimSteeringDown   = FVector::ZeroVector;
		SimSteeringLeft   = FVector::ZeroVector;
		SimSteeringRight  = FVector::ZeroVector;
		SimSteeringResult = FVector::ZeroVector;
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

	const float   AgentRadius   = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FVector ActorLocation = GetActorLocation();

	// Proximity: unconditionally advance when within agent radius of the current node.
	if (SimPathNodeIndex < Path.Num() - 1 &&
		FVector::Distance(ActorLocation, Path[SimPathNodeIndex]) <= AgentRadius)
	{
		++SimPathNodeIndex;
		return;
	}

	// Skip ahead: if the node beyond the current one is already directly reachable,
	// target it now for a smoother arc rather than threading through each waypoint precisely.
	if (SimPathNodeIndex < Path.Num() - 1)
	{
		const bool bNextClear = !World->LineTraceTestByChannel(ActorLocation, Path[SimPathNodeIndex + 1], ECC_WorldStatic);
		if (bNextClear)
		{
			++SimPathNodeIndex;
			return;
		}
	}

	// Fall back: if the current node is no longer directly reachable (overshot into
	// geometry), retreat so steering can re-establish a clear avenue of approach.
	if (SimPathNodeIndex > 0 &&
		FVector::Distance(ActorLocation, Path[SimPathNodeIndex]) > AgentRadius)
	{
		const bool bCurrentBlocked = World->LineTraceTestByChannel(ActorLocation, Path[SimPathNodeIndex], ECC_WorldStatic);
		if (bCurrentBlocked)
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

	// Commit to deceleration once within stopping distance of ApproachLocation.
	// This fires independently of which path node is current — ApproachLocation is an intercept
	// along the path to the target, not the path's final node.
	if (!bDecelerating && ApproachLocation.X < FLT_MAX && SimAcceleration > KINDA_SMALL_NUMBER)
	{
		const FVector ToApproach     = ApproachLocation - GetActorLocation();
		const float   DistToApproach = ToApproach.Size();
		const bool bVelocityNegligible = SimVelocity.IsNearlyZero();
		const bool bHeadingToward      = !bVelocityNegligible && FVector::DotProduct(SimVelocity.GetSafeNormal(), ToApproach.GetSafeNormal()) > 0.0f;
		if (bVelocityNegligible || bHeadingToward)
		{
			const float StoppingDistance = (CurrentSpeed * CurrentSpeed) / (2.0f * SimAcceleration);
			if (DistToApproach <= StoppingDistance)
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

	// Build a path direction: toward the node, corrected for any velocity that is
	// perpendicular or away from the node (forward-toward velocity is left uncontested).
	FVector NodeAccelDir = ToNode;
	if (!SimVelocity.IsNearlyZero())
	{
		const float ForwardSpeed      = FVector::DotProduct(SimVelocity, ToNode);
		const FVector TowardComponent = FMath::Max(0.0f, ForwardSpeed) * ToNode;
		NodeAccelDir                  = ToNode - (SimVelocity - TowardComponent);
	}

	const FVector AccelerationVector = (NodeAccelDir.GetSafeNormal() + SimSteeringResult).GetSafeNormal() * SimAcceleration * DeltaTime;
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
		else if (ApproachLocation.X < FLT_MAX)
		{
			const FVector ToApproach = (ApproachLocation - ActorLocation).GetSafeNormal();
			if (ToApproach.IsNearlyZero())
			{
				return;
			}
			TargetRotation = ToApproach.Rotation();
		}
		else if (!SimPathSearchSpace.PathSolution.IsEmpty())
		{
			const FVector ToPathEnd = (SimPathSearchSpace.PathSolution.Last() - ActorLocation).GetSafeNormal();
			if (ToPathEnd.IsNearlyZero())
			{
				return;
			}
			TargetRotation = ToPathEnd.Rotation();
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
	const bool bHasApproach  = ApproachLocation.X < FLT_MAX;
	const bool bHasValidPath = SimPathSearchSpace.Status == EPathSearchStatus::Success
	                           && !SimPathSearchSpace.PathSolution.IsEmpty();
	if (!bHasApproach && !bHasValidPath)
	{
		return;
	}

	UpdateSimRotation();

	if (bSettled)
	{
		return;
	}

	if (bHasApproach && IsAtGoalLocation() && SimVelocity.IsNearlyZero())
	{
		SimVelocity = FVector::ZeroVector;
		bSettled    = true;
		return;
	}

	// Gate on velocity direction only when an approach is set — brake if headed away from it.
	if (bHasApproach && !SimVelocity.IsNearlyZero())
	{
		const FVector ToApproach = (ApproachLocation - GetActorLocation()).GetSafeNormal();
		const float   CosAngle   = FMath::Cos(FMath::DegreesToRadians(SimPathingAngle));
		if (FVector::DotProduct(SimVelocity.GetSafeNormal(), ToApproach) < CosAngle)
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

	const int32 ClampedNodeIndex = FMath::Clamp(SimPathNodeIndex, 0, Path.Num() - 1);

	float RemainingDistance = InDistance;
	int32 NextSegmentIndex; // first segment index to walk after finding the start point

	if (ClampedNodeIndex == 0 || Path.Num() == 1)
	{
		// No previous node — back out dist-to-origin from InDistance, then walk from Path[0].
		RemainingDistance = FMath::Max(0.0f, InDistance - FVector::Distance(InCurrentLocation, Path[0]));
		NextSegmentIndex  = 0;
	}
	else
	{
		// Project current location onto segment [ClampedNodeIndex-1, ClampedNodeIndex].
		const FVector SegA     = Path[ClampedNodeIndex - 1];
		const FVector SegB     = Path[ClampedNodeIndex];
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
		NextSegmentIndex   = ClampedNodeIndex; // continue from SegB = Path[ClampedNodeIndex]
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

void ANavGen3DMover::UpdateSimSteering()
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

	const float DeltaTime      = World->GetDeltaSeconds();
	const float SteeringFactor = DeltaTime / FMath::Max(SimSteeringTime, 0.1f);

	// Always decay regardless of state.
	SimSteeringUp    *= (1.0f - SteeringFactor);
	SimSteeringDown  *= (1.0f - SteeringFactor);
	SimSteeringLeft  *= (1.0f - SteeringFactor);
	SimSteeringRight *= (1.0f - SteeringFactor);

	// Only run traces while actively path-following (not decelerating to destination).
	const bool bShouldTrace = !bDecelerating &&
	                          SimPathSearchSpace.Status == EPathSearchStatus::Success &&
	                          !SimPathSearchSpace.PathSolution.IsEmpty();
	if (!bShouldTrace)
	{
		SimSteeringResult = (SimSteeringUp + SimSteeringDown + SimSteeringLeft + SimSteeringRight) * 0.25f;
		return;
	}

	const FVector ActorLocation = GetActorLocation();
	const float   AgentRadius   = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const float   TraceLength   = FMath::Max(AgentRadius, SimVelocity.Size() * DeltaTime * 3.0f);

	// Base direction: SimVelocity projected to the XY plane, falling back to actor forward if negligible.
	const FVector Velocity2D  = FVector(SimVelocity.X, SimVelocity.Y, 0.0f);
	const FVector ActorFwd    = GetActorForwardVector();
	const FVector ActorFwd2D  = FVector(ActorFwd.X, ActorFwd.Y, 0.0f);
	const FVector ForwardDir  = !Velocity2D.IsNearlyZero() ? Velocity2D.GetSafeNormal()
	                          : !ActorFwd2D.IsNearlyZero() ? ActorFwd2D.GetSafeNormal()
	                          : FVector::ForwardVector;
	const float   AngleRad   = FMath::DegreesToRadians(SimSteeringAngle);

	// SimSteeringUp: starts above the actor, pitch ForwardDir upward by SimSteeringAngle.
	const FVector UpTraceStart = ActorLocation + FVector::UpVector * AgentRadius;
	const FVector UpTraceDir   = FMath::Cos(AngleRad) * ForwardDir + FMath::Sin(AngleRad) * FVector::UpVector;
	const FVector UpTraceEnd   = UpTraceStart + UpTraceDir * TraceLength;
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, UpTraceStart, UpTraceEnd, ECC_WorldStatic);
		if (bHit && FVector::DotProduct(Hit.ImpactPoint - ActorLocation, ForwardDir) > 0.0f)
		{
			SimSteeringUp = (SimSteeringUp + FVector::DownVector * SteeringFactor).GetClampedToMaxSize(1.0f);
		}
	}

	// SimSteeringDown: starts below the actor, pitch ForwardDir downward by SimSteeringAngle.
	const FVector DownTraceStart = ActorLocation - FVector::UpVector * AgentRadius;
	const FVector DownTraceDir   = FMath::Cos(AngleRad) * ForwardDir - FMath::Sin(AngleRad) * FVector::UpVector;
	const FVector DownTraceEnd   = DownTraceStart + DownTraceDir * TraceLength;
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, DownTraceStart, DownTraceEnd, ECC_WorldStatic);
		if (bHit && FVector::DotProduct(Hit.ImpactPoint - ActorLocation, ForwardDir) > 0.0f)
		{
			SimSteeringDown = (SimSteeringDown + FVector::UpVector * SteeringFactor).GetClampedToMaxSize(1.0f);
		}
	}

	// Shared side axes for left/right traces.
	const FVector SideRight = FVector::CrossProduct(FVector::UpVector, ForwardDir).GetSafeNormal();
	const FVector SideLeft  = -SideRight;

	// SimSteeringLeft: start offset left, trace rotated outward to the left; adjustment pushes right.
	const FVector LeftTraceStart = ActorLocation + SideLeft * AgentRadius;
	const FVector LeftTraceDir   = FMath::Cos(AngleRad) * ForwardDir + FMath::Sin(AngleRad) * SideLeft;
	const FVector LeftTraceEnd   = LeftTraceStart + LeftTraceDir * TraceLength;
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, LeftTraceStart, LeftTraceEnd, ECC_WorldStatic);
		if (bHit && FVector::DotProduct(Hit.ImpactPoint - ActorLocation, ForwardDir) > 0.0f)
		{
			SimSteeringLeft = (SimSteeringLeft + SideRight * SteeringFactor).GetClampedToMaxSize(1.0f);
		}
	}

	// SimSteeringRight: start offset right, trace rotated outward to the right; adjustment pushes left.
	const FVector RightTraceStart = ActorLocation + SideRight * AgentRadius;
	const FVector RightTraceDir   = FMath::Cos(AngleRad) * ForwardDir + FMath::Sin(AngleRad) * SideRight;
	const FVector RightTraceEnd   = RightTraceStart + RightTraceDir * TraceLength;
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, RightTraceStart, RightTraceEnd, ECC_WorldStatic);
		if (bHit && FVector::DotProduct(Hit.ImpactPoint - ActorLocation, ForwardDir) > 0.0f)
		{
			SimSteeringRight = (SimSteeringRight + SideLeft * SteeringFactor).GetClampedToMaxSize(1.0f);
		}
	}

	SimSteeringResult = (SimSteeringUp + SimSteeringDown + SimSteeringLeft + SimSteeringRight) * 0.25f;
}

void ANavGen3DMover::SimDrawSteering()
{
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem || Subsystem->DebugLevel < 2)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float   DrawTime      = World->GetDeltaSeconds() * 1.1f;
	const FVector ActorLocation = GetActorLocation();
	const float   AgentRadius   = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const float   TraceLength   = FMath::Max(AgentRadius, SimVelocity.Size() * World->GetDeltaSeconds() * 3.0f);

	const FVector Velocity2D = FVector(SimVelocity.X, SimVelocity.Y, 0.0f);
	const FVector ActorFwd   = GetActorForwardVector();
	const FVector ActorFwd2D = FVector(ActorFwd.X, ActorFwd.Y, 0.0f);
	const FVector ForwardDir = !Velocity2D.IsNearlyZero() ? Velocity2D.GetSafeNormal()
	                         : !ActorFwd2D.IsNearlyZero() ? ActorFwd2D.GetSafeNormal()
	                         : FVector::ForwardVector;
	const float   AngleRad  = FMath::DegreesToRadians(SimSteeringAngle);
	const FVector SideRight = FVector::CrossProduct(FVector::UpVector, ForwardDir).GetSafeNormal();
	const FVector SideLeft  = -SideRight;

	const FVector UpTraceStart    = ActorLocation + FVector::UpVector * AgentRadius;
	const FVector DownTraceStart  = ActorLocation - FVector::UpVector * AgentRadius;
	const FVector LeftTraceStart  = ActorLocation + SideLeft  * AgentRadius;
	const FVector RightTraceStart = ActorLocation + SideRight * AgentRadius;

	const FVector UpTraceEnd    = UpTraceStart    + (FMath::Cos(AngleRad) * ForwardDir + FMath::Sin(AngleRad) * FVector::UpVector)  * TraceLength;
	const FVector DownTraceEnd  = DownTraceStart  + (FMath::Cos(AngleRad) * ForwardDir - FMath::Sin(AngleRad) * FVector::UpVector)  * TraceLength;
	const FVector LeftTraceEnd  = LeftTraceStart  + (FMath::Cos(AngleRad) * ForwardDir + FMath::Sin(AngleRad) * SideLeft)           * TraceLength;
	const FVector RightTraceEnd = RightTraceStart + (FMath::Cos(AngleRad) * ForwardDir + FMath::Sin(AngleRad) * SideRight)          * TraceLength;

	DrawDebugLine(World, UpTraceStart,    UpTraceEnd,    FColor::Yellow, false, DrawTime);
	DrawDebugLine(World, DownTraceStart,  DownTraceEnd,  FColor::Yellow, false, DrawTime);
	DrawDebugLine(World, LeftTraceStart,  LeftTraceEnd,  FColor::Yellow, false, DrawTime);
	DrawDebugLine(World, RightTraceStart, RightTraceEnd, FColor::Yellow, false, DrawTime);

	if (!SimSteeringResult.IsNearlyZero())
	{
		DrawDebugLine(World, ActorLocation, ActorLocation + SimSteeringResult * TraceLength, FColor::Orange, false, DrawTime);
	}
}

void ANavGen3DMover::SimDrawPath()
{
	const UNavGen3DSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UNavGen3DSubsystem>() : nullptr;
	if (!Subsystem || Subsystem->DebugLevel < 1)
	{
		return;
	}

	const UWorld* World    = GetWorld();
	const float   DrawTime = World ? World->GetDeltaSeconds() * 1.1f : 0.0f;
	SimPathSearchSpace.DebugDrawPath(DrawTime);

	if (World && ApproachLocation.X < FLT_MAX)
	{
		const float Radius = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
		DrawDebugSphere(World, ApproachLocation, Radius, 12, FColor::Orange, /*bPersistentLines=*/false, DrawTime);
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

	SimDrawPath();
	SimDrawSteering();
}

NavGen3D_ENABLE_OPTIMIZATION
