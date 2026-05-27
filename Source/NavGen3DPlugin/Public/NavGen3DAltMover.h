// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DAltMover.generated.h"

UCLASS()
class NAVGEN3DPLUGIN_API ANavGen3DAltMover : public AActor
{
	GENERATED_BODY()

public:
	ANavGen3DAltMover();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	int32 AgentIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	FVector SimVelocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float SimMaxVelocity = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float SimAcceleration = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float SimTurnRate = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float SimPathingAngle = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float ApproachDistMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float ApproachDistMax = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	float SimBrakingMultiplier = 3.0f;

	void SimReset(bool bResetPhysics);

	inline static bool bDebugDrawAsArrow = true;

private:
	void MoveWithSweep(const FVector& NewLocation);
	void DebugDraw();
	void SimUpdateMovement(float DeltaTime);
	void UpdateSimPathFollowing(float DeltaTime);
	void UpdateSimApproachTarget(float DeltaTime);
	void UpdateSimPathNode();
	bool IsApproachingTarget();
	FVector GetTargetLocation() const;

	TSharedPtr<FPathSearchSpace> SimPathSearchSpace;
	int32   SimPathIndex              = 0;
	FVector SimPathVector             = FVector::ZeroVector;
	FVector SimApproachVector         = FVector::ZeroVector;
	FVector SimApproachTargetLocation = FVector(FLT_MAX);
	float   SimApproachTimer          = 0.0f;
	bool    bIsApproachingTarget      = false;
	bool    bSettled                  = false;
};
