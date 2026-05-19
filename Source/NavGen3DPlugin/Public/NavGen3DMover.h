// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DMover.generated.h"

UCLASS()
class NAVGEN3DPLUGIN_API ANavGen3DMover : public AActor
{
	GENERATED_BODY()

public:
	ANavGen3DMover();

	virtual void BeginPlay() override;
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
	float ApproachDistance = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	FVector ApproachLocation = FVector(FLT_MAX);

private:
	void DebugDrawMover();
	void SimDrawPath();
	void SimDrawSteering();
	void UpdateApproachLocation();
	void UpdateSimLocation();
	void UpdateSimRotation();
	void UpdateSimSteering();
	bool IsAtGoalLocation() const;
	bool UpdateSimPathFollowing();
	void UpdateSimPathNode();
	void SimReset(bool bResetPhysics = false);
	bool SimPathLookAheadLocation(const FVector& InCurrentLocation, float InDistance, FVector& OutPathLocation) const;

	FPathSearchSpace SimPathSearchSpace;
	int32   SimPathNodeIndex  = 0;
	bool    bSettled          = false;
	bool    bFacingPath       = true;
	bool    bDecelerating     = false;
	FVector SimSteeringUp        = FVector::ZeroVector;
	FVector SimSteeringDown      = FVector::ZeroVector;
	FVector SimSteeringLeft      = FVector::ZeroVector;
	FVector SimSteeringRight     = FVector::ZeroVector;
	FVector SimSteeringResult    = FVector::ZeroVector;

	inline static float SimSteeringTime  = 2.0f;
	inline static float SimSteeringAngle = 30.0f;
};
