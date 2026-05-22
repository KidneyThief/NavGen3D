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
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugMoverMaxVelocity.IsSet())
			SimMaxVelocity = Subsystem->DebugMoverMaxVelocity.GetValue();
		if (Subsystem->DebugMoverAcceleration.IsSet())
			SimAcceleration = Subsystem->DebugMoverAcceleration.GetValue();

	}
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
			const float MinZ = Hit.ImpactPoint.Z + LiftRadius;
			if (TargetLocation.Z < MinZ)
			{
				TargetLocation.Z = MinZ;
			}
		}
	}

	const float DistToTarget  = FVector::Distance(GetActorLocation(), TargetLocation);
	const float AgentRadius   = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FCollisionShape LOSSphere = FCollisionShape::MakeSphere(AgentRadius * 0.5f);

	// When far from the target, navigate directly without trying to find an approach intercept.
	// Only enter approach-computation logic once within 2x ApproachDistance to prevent the
	// approach point from shifting every frame as the actor moves and resetting bDecelerating.
	if (DistToTarget > ApproachDistance * 2.0f)
	{
		if (ApproachLocation.X < FLT_MAX)
		{
			ApproachLocation = FVector(FLT_MAX);
			SimReset();
		}
		if (!bSettled)
		{
			SimPathSearchSpace.Initialize(Subsystem, AgentIndex, GetActorLocation(), TargetLocation);
			Subsystem->PathFind(SimPathSearchSpace);
		}
		return;
	}

	// Determine whether the current ApproachLocation is still valid.
	// Only invalidate on overshoot or lost LOS — the approach point is fixed relative to
	// the target, so distance-to-target alone is not a reason to discard it.
	bool bNeedNewApproach = (ApproachLocation.X >= FLT_MAX);
	if (!bNeedNewApproach)
	{
		const bool bTooClose = DistToTarget < ApproachDistance * 0.5f;
		const bool bLOSLost  = World->SweepTestByChannel(ApproachLocation, TargetLocation, FQuat::Identity, ECC_Visibility, LOSSphere);
		bNeedNewApproach = bTooClose || bLOSLost;
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

	// Find approach point: walk backward from the target along the path by ApproachDistance.
	// This gives a stable point fixed relative to the target rather than a lookahead that
	// moves with the actor — preventing bApproachChanged from firing every frame.
	const TArray<FVector>& Sol = PathToTarget.PathSolution;
	float TotalPathLen = 0.0f;
	for (int32 i = 0; i + 1 < Sol.Num(); ++i)
	{
		TotalPathLen += FVector::Distance(Sol[i], Sol[i + 1]);
	}

	const FVector PreviousApproach = ApproachLocation;

	if (TotalPathLen > ApproachDistance)
	{
		const float WalkDist = TotalPathLen - ApproachDistance;
		FVector CandidatePoint = Sol.Last();
		PathLookAhead(Sol, Sol[0], WalkDist, CandidatePoint);

		const bool bHasLOS = !World->SweepTestByChannel(CandidatePoint, TargetLocation, FQuat::Identity, ECC_Visibility, LOSSphere);
		ApproachLocation   = bHasLOS ? CandidatePoint : FVector(FLT_MAX);
	}
	else
	{
		// Already inside the approach zone — stop at the path terminus so the mover
		// decelerates rather than flying at full speed with no deceleration gate.
		ApproachLocation = Sol.Last();
	}

	SimPathSearchSpace = PathToTarget;

	// Only reset if the approach location changed by more than AgentRadius. The 1-unit
	// threshold fires on every slight target movement, resetting bDecelerating each frame.
	const bool bApproachChanged = (PreviousApproach.X >= FLT_MAX) != (ApproachLocation.X >= FLT_MAX)
	    || (ApproachLocation.X < FLT_MAX && FVector::DistSquared(ApproachLocation, PreviousApproach) > FMath::Square(AgentRadius));
	if (bApproachChanged)
	{
		SimReset();
	}
}

bool ANavGen3DMover::IsAtGoalLocation(bool& OutHasLOS) const
{
	OutHasLOS = false;

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

	// OutHasLOS: mover is facing within SimPathingAngle of the target and has clear LOS to it.
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
	if (TargetLocation.X < FLT_MAX)
	{
		const FVector ToTarget  = TargetLocation - GetActorLocation();
		const float   TargetDist = ToTarget.Size();
		if (TargetDist > KINDA_SMALL_NUMBER)
		{
			const float AngleToTarget = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(FVector::DotProduct(GetActorForwardVector(), ToTarget / TargetDist), -1.0f, 1.0f)));
			const bool bInAngle = AngleToTarget < SimPathingAngle;
			const bool bLOS     = !World->LineTraceTestByChannel(GetActorLocation(), TargetLocation, ECC_Visibility);
			OutHasLOS = bInAngle && bLOS;
		}
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
		SimSteeringUp       = FVector::ZeroVector;
		SimSteeringDown     = FVector::ZeroVector;
		SimSteeringLeft     = FVector::ZeroVector;
		SimSteeringRight    = FVector::ZeroVector;
		SimSteeringResult   = FVector::ZeroVector;
		SimBlockedSteering  = FVector::ZeroVector;
		SimSteeringInfluence = 0.5f;
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

	if (SimPathNodeIndex >= Path.Num() - 1)
	{
		return;
	}

	const float   AgentRadius   = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FVector ActorLocation = GetActorLocation();
	const FVector CurrentNode   = Path[SimPathNodeIndex];

	bool bShouldAdvance = false;

	// Overshot: actor has passed the current node along the path segment direction.
	if (SimPathNodeIndex > 0)
	{
		const FVector Seg    = CurrentNode - Path[SimPathNodeIndex - 1];
		const float SegLenSq = Seg.SizeSquared();
		if (SegLenSq > KINDA_SMALL_NUMBER)
		{
			const float t = FVector::DotProduct(ActorLocation - Path[SimPathNodeIndex - 1], Seg) / SegLenSq;
			bShouldAdvance = (t > 1.0f);
		}
	}

	// Within proximity of current node — advance unconditionally.
	// The path was already validated by the navmesh; the sweep to the next node can fail
	// spuriously (bStartPenetrating) when the actor is at or near the volume center,
	// and MoveWithSweep prevents actual wall penetration regardless.
	if (!bShouldAdvance && FVector::Distance(ActorLocation, CurrentNode) <= AgentRadius)
	{
		bShouldAdvance = true;
	}

	// Current node is blocked from here (geometry between actor and node) — advance so the
	// path target stays ahead of the mover rather than pulling it into blocked geometry.
	// Guard against bStartPenetrating: if the actor itself is inside geometry, this is a
	// different problem; don't blindly skip nodes from an unreliable sweep origin.
	if (!bShouldAdvance)
	{
		FHitResult Hit;
		const FCollisionShape AgentSphere = FCollisionShape::MakeSphere(AgentRadius);
		const bool bBlocked = World->SweepSingleByChannel(Hit, ActorLocation, CurrentNode, FQuat::Identity, ECC_WorldStatic, AgentSphere);
		bShouldAdvance = bBlocked && !Hit.bStartPenetrating;
	}

	if (bShouldAdvance)
	{
		// Scan the entire remaining path from N+1 to find the first node reachable via a clear sweep.
		// If no clear node exists, do NOT advance — the yellow debug line must never point through geometry.
		const FCollisionShape AgentSphere = FCollisionShape::MakeSphere(AgentRadius);
		const int32 FirstCandidate = SimPathNodeIndex + 1;

		for (int32 i = FirstCandidate; i < Path.Num(); ++i)
		{
			FHitResult SweepHit;
			const bool bNextBlocked = World->SweepSingleByChannel(SweepHit, ActorLocation, Path[i], FQuat::Identity, ECC_WorldStatic, AgentSphere);
			if (!bNextBlocked)
			{
				SimPathNodeIndex = i;
				break;
			}
		}
	}
}

void ANavGen3DMover::MoveWithSweep(const FVector& NewLocation)
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

	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByChannel(Hit, GetActorLocation(), NewLocation, FQuat::Identity, ECC_WorldStatic, AgentSphere);

	const float DeltaTime      = World->GetDeltaSeconds();
	const float SteeringFactor = DeltaTime / FMath::Max(SimSteeringTime, 0.1f);

	if (bBlocked && Hit.bStartPenetrating)
	{
		SimSteeringInfluence = FMath::Min(SimSteeringInfluence + SteeringFactor, 2.0f); // kept for debug viz only
		// Push out via the MTD normal when available; otherwise move toward the intended
		// destination so the actor doesn't freeze indefinitely inside geometry.
		const FVector EscapeLocation = Hit.Normal.IsNearlyZero()
		                             ? NewLocation
		                             : GetActorLocation() + Hit.Normal * (Hit.PenetrationDepth + 1.0f);
		SetActorLocation(EscapeLocation);
		return;
	}

	SetActorLocation(bBlocked ? Hit.Location : NewLocation);

	if (bBlocked)
	{
		// Cancel the velocity component heading into the surface.
		const float VelIntoSurface = FVector::DotProduct(SimVelocity, Hit.Normal);
		if (VelIntoSurface < 0.0f)
		{
			SimVelocity += (-VelIntoSurface) * Hit.Normal;
		}

		SimSteeringInfluence = FMath::Min(SimSteeringInfluence + SteeringFactor, 2.0f); // kept for debug viz only
		SimBlockedSteering   = (SimBlockedSteering + Hit.Normal * SteeringFactor).GetClampedToMaxSize(1.0f);
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
		MoveWithSweep(GetActorLocation() + SimVelocity * DeltaTime);
		return true;
	}

	// Steer directly toward the node with obstacle avoidance blended in.
	// The velocity-correction formula (ToNode - lateral_velocity) caused jitter:
	// wall bounces created large lateral velocity, making NodeAccelDir oscillate
	// each frame in the opposite direction of the last impact normal.
	// When opposing probe directions cancel, EffectiveSteering → 0, so the mover
	// accelerates at full rate toward the node with no steering tax.
	const FVector EffectiveSteering  = (SimSteeringResult + SimBlockedSteering).GetClampedToMaxSize(1.0f);
	const FVector AccelerationVector = (ToNode + EffectiveSteering * 2.0f).GetSafeNormal() * SimAcceleration * DeltaTime;
	SimVelocity = (SimVelocity + AccelerationVector).GetClampedToMaxSize(SimMaxVelocity);
	MoveWithSweep(GetActorLocation() + SimVelocity * DeltaTime);

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

	bool bGoalLOS = false;
	if (bHasApproach && IsAtGoalLocation(bGoalLOS) && SimVelocity.IsNearlyZero())
	{
		SimVelocity = FVector::ZeroVector;
		bSettled    = true;
	}

	UpdateSimRotation();

	if (bSettled)
	{
		return;
	}

	UpdateSimPathFollowing();
}

bool ANavGen3DMover::SimPathLookAheadLocation(const FVector& InCurrentLocation, float InDistance, FVector& OutPathLocation) const
{
	return PathLookAhead(SimPathSearchSpace.PathSolution, InCurrentLocation, InDistance, OutPathLocation);
}

bool ANavGen3DMover::PathLookAhead(const TArray<FVector>& InPath, const FVector& InCurrentLocation, float InDistance, FVector& OutLocation)
{
	if (InPath.IsEmpty())
	{
		return false;
	}

	// Find the nearest point on the path to InCurrentLocation by projecting onto each segment.
	FVector NearestPoint = InPath[0];
	int32   NearestSeg   = 0;
	float   NearestDistSq = FVector::DistSquared(InCurrentLocation, InPath[0]);

	for (int32 i = 0; i + 1 < InPath.Num(); ++i)
	{
		const FVector AB     = InPath[i + 1] - InPath[i];
		const float   ABLenSq = AB.SizeSquared();
		if (ABLenSq < KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const float   t    = FMath::Clamp(FVector::DotProduct(InCurrentLocation - InPath[i], AB) / ABLenSq, 0.0f, 1.0f);
		const FVector Proj = InPath[i] + AB * t;
		const float   DistSq = FVector::DistSquared(InCurrentLocation, Proj);
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
		if (SegLen < KINDA_SMALL_NUMBER)
		{
			continue;
		}
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
	SimSteeringUp       *= (1.0f - SteeringFactor);
	SimSteeringDown     *= (1.0f - SteeringFactor);
	SimSteeringLeft     *= (1.0f - SteeringFactor);
	SimSteeringRight    *= (1.0f - SteeringFactor);
	// SimBlockedSteering decays 4× faster than probe steering — wall-bounce normals from
	// MoveWithSweep should clear quickly so opposing impact normals don't linger and oscillate.
	SimBlockedSteering  *= (1.0f - FMath::Min(DeltaTime / FMath::Max(SimSteeringTime * 0.25f, 0.05f), 1.0f));
	// Decay influence back toward base (0.5); excess above base decays at the same rate.
	SimSteeringInfluence = 0.5f + (SimSteeringInfluence - 0.5f) * (1.0f - SteeringFactor);

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
	const float   TraceLength   = FMath::Max(AgentRadius * 4.0f, SimVelocity.Size() * DeltaTime * 3.0f);

	// Base direction: path-node direction is preferred since SimPathNodeIndex is now stable
	// (advance-only). This orients probes toward the actual navigation target rather than
	// the actor's lagging rotation, giving more relevant obstacle data in tight geometry.
	// ActorFwd2D is the fallback when no path node is available.
	const FVector ActorFwd   = GetActorForwardVector();
	const FVector ActorFwd2D = FVector(ActorFwd.X, ActorFwd.Y, 0.0f);
	const TArray<FVector>& SteerPath    = SimPathSearchSpace.PathSolution;
	const int32            SteerNodeIdx = FMath::Clamp(SimPathNodeIndex, 0, SteerPath.Num() - 1);
	const FVector NodeDir2D = SteerPath.IsEmpty() ? FVector::ZeroVector
	                        : FVector(SteerPath[SteerNodeIdx].X - ActorLocation.X,
	                                  SteerPath[SteerNodeIdx].Y - ActorLocation.Y, 0.0f);
	const FVector ForwardDir = !NodeDir2D.IsNearlyZero()   ? NodeDir2D.GetSafeNormal()
	                         : !ActorFwd2D.IsNearlyZero()   ? ActorFwd2D.GetSafeNormal()
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
			const float ProximityWeight = 1.0f - Hit.Time;
			SimSteeringUp = (SimSteeringUp + FVector::DownVector * SteeringFactor * ProximityWeight).GetClampedToMaxSize(1.0f);
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
			const float ProximityWeight = 1.0f - Hit.Time;
			SimSteeringDown = (SimSteeringDown + FVector::UpVector * SteeringFactor * ProximityWeight).GetClampedToMaxSize(1.0f);
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
			const float ProximityWeight = 1.0f - Hit.Time;
			SimSteeringLeft = (SimSteeringLeft + SideRight * SteeringFactor * ProximityWeight).GetClampedToMaxSize(1.0f);
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
			const float ProximityWeight = 1.0f - Hit.Time;
			SimSteeringRight = (SimSteeringRight + SideLeft * SteeringFactor * ProximityWeight).GetClampedToMaxSize(1.0f);
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
	const float   TraceLength   = FMath::Max(AgentRadius * 4.0f, SimVelocity.Size() * World->GetDeltaSeconds() * 3.0f);

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
		const FColor Purple(128, 0, 200);
		const FVector SteeringEnd = ActorLocation + SimSteeringResult.GetSafeNormal() * 200.0f;
		DrawDebugDirectionalArrow(World, ActorLocation, SteeringEnd, 20.0f, Purple, false, DrawTime, 0, 2.0f);
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

	const TArray<FVector>& Path = SimPathSearchSpace.PathSolution;
	if (World && Path.IsValidIndex(SimPathNodeIndex))
	{
		const FVector NodeLocation = Path[SimPathNodeIndex];
		DrawDebugDirectionalArrow(World, GetActorLocation(), NodeLocation, 20.0f, FColor::Yellow, false, DrawTime, 0, 2.0f);
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

	const float   Radius  = Subsystem->GetAgentCollisionRadius(AgentIndex, /*bPadded=*/false);
	const FVector Center  = GetActorLocation();
	bool bHasLOS = false;
	IsAtGoalLocation(bHasLOS);
	const FColor DrawColor = bHasLOS ? FColor::Green : FColor::Orange;

	if (bDebugDrawAsArrow)
	{
		const float   ArrowLength = Radius * 2.0f;
		const float   Thickness   = 1.0f;
		const FVector Forward     = GetActorForwardVector();
		const FVector Start       = Center - Forward * Radius;
		const FVector End         = Center + Forward * Radius;

		const float   ShaftLength = ArrowLength * 0.7f;
		const FVector ShaftEnd    = Start + Forward * ShaftLength;
		DrawDebugCylinder(World, Start, ShaftEnd, ArrowLength * 0.05f, 8, DrawColor, false, -1.0f, 0, Thickness);

		const float HeadHalfAngle = FMath::Atan2(ArrowLength * 0.15f, ArrowLength * 0.3f);
		DrawDebugCone(World, End, -Forward, ArrowLength * 0.3f, HeadHalfAngle, HeadHalfAngle, 8, DrawColor, false, -1.0f, 0, Thickness);
	}
	else
	{
		// Saucer: thin torus rim + larger central dome sphere
		const float MajorRadius  = Radius;
		const float MinorRadius  = Radius * 0.15f;  // thin rim
		const float SphereRadius = Radius * 0.7f;
		const FColor SaucerColor = bHasLOS ? FColor(220, 180, 255) : FColor(180, 120, 220);
		const FColor RimColor    = SaucerColor;
		const FColor DomeColor   = SaucerColor;
		const int32  NumSegments = 16;
		const float  Step        = TWO_PI / NumSegments;

		auto TorusPoint = [&](float U, float V) -> FVector
		{
			return Center + FVector(
				(MajorRadius + MinorRadius * FMath::Cos(V)) * FMath::Cos(U),
				(MajorRadius + MinorRadius * FMath::Cos(V)) * FMath::Sin(U),
				MinorRadius * FMath::Sin(V));
		};

		for (int32 i = 0; i < NumSegments; ++i)
		{
			const float U0 = i * Step, U1 = (i + 1) * Step;
			for (int32 j = 0; j < NumSegments; ++j)
			{
				const float V0 = j * Step, V1 = (j + 1) * Step;
				DrawDebugLine(World, TorusPoint(U0, V0), TorusPoint(U1, V0), RimColor, false, -1.0f);
				DrawDebugLine(World, TorusPoint(U0, V0), TorusPoint(U0, V1), RimColor, false, -1.0f);
			}
		}

		DrawDebugSphere(World, Center, SphereRadius, 8, DomeColor, false, -1.0f);
	}

	SimDrawPath();
	SimDrawSteering();
}

NavGen3D_ENABLE_OPTIMIZATION
