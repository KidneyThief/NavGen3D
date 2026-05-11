// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DLog.h"
#include "NavMeshVolume.h"
#include "NavVolumeConnection.h"
#include "NavGen3DSubsystem.generated.h"

UENUM()
enum class ENavGen3DDrawMode : uint8
{
	None               UMETA(DisplayName = "None"),
	NavBounds3D        UMETA(DisplayName = "Nav Bounds 3D"),
	NavMesh3D          UMETA(DisplayName = "Nav Mesh 3D"),
	UnprocessedVolumes UMETA(DisplayName = "Unprocessed Volumes"),
};

class SNavGen3DWindow;

UCLASS()
class NAVGEN3DPLUGIN_API UNavGen3DSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& InCollection) override;
	virtual void Deinitialize() override;

	void OnEndFrame();

	TArray<TObjectPtr<ANavGen3DBoundsVolume>> GetBoundsVolumes();
	void AddBoundsVolume(ANavGen3DBoundsVolume* InVolume);
	void RemoveBoundsVolume(ANavGen3DBoundsVolume* InVolume);
	void InitializeNavMesh3D();
	bool GenerateNavMesh3DFromBoundsVolume(ANavGen3DBoundsVolume* InVolume);
	bool GenerateNavMesh3D(NavMeshVolume* InVolume = nullptr);
	bool PlaneTrace(FVector InMin, FVector InMax, EAxis::Type InAxis, FVector& OutImpactPoint, bool& OutStartPenetrating);
	bool ValidateConnectionCollision(const FCollisionShape& InCapsule, const FVector& InLocation);
	bool AddNavMeshVolume(NavMeshVolume& RefVolume);
	void RemoveNavMeshVolume(uint64 InID);
	bool ProcessNavMeshVolume(NavMeshVolume& RefVolume, bool InDrawDebug = false);
	void ValidateEmbeddedBoundsVolumes();
	bool ValidateNavMesh3D(NavMeshVolume* InVolume = nullptr);
	void SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> InWindow);
	void AddLogMessage(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage);

	bool IsPlayMode();
	FVector GetCameraLocation();
	bool GetAgentSettings(int32 InIndex, float& OutRadius, float& OutHeight) const;
	int32 GetSupportedAgentCount() const;
	bool GenerateNavMesh3DConnections(int32 InAgentIndex);
	NavMeshVolume* FindNavMeshVolumeByID(uint64 InID);
	void FindNavMeshVolumeConnections(int32 InAgentIndex, const FCollisionShape& InCapsule, const NavMeshVolume& InSourceVolume);
	bool FindNavMeshVolumeConnection(int32 InAgentIndex, const FCollisionShape& InCapsule, const NavMeshVolume& InSourceVolume, const NavMeshVolume& InNeighborVolume, int32 InAxis, FVector& OutLocation);
	const TArray<NavVolumeConnection>* GetVolumeConnections(int32 InAgentIndex, uint64 InVolumeID) const;

	static inline FString FVectorToString(const FVector& InVec)
	{
		return FString::Printf(TEXT("%.2f, %.2f, %.2f"), InVec.X, InVec.Y, InVec.Z);
	}
	int32 GetProcessVolumesCount() const { return ProcessVolumesList.Num(); }
	int32 GetSolutionVolumesCount() const { return NavMeshSolutionMap.Num(); }
	uint64 CalculateHash3D(const FVector& InVec) const;
	NavMeshVolume* FindVolumeContainingLocation(const FVector& InLocation);
	TOptional<NavMeshVolume> FindGenerationVolumeContainingLocation(const FVector& InLocation, bool InRemoveFromProcessing);
	TOptional<NavMeshVolume> FindClosestGenerationVolume(const FVector& InLocation, bool InRemoveFromProcessing);

	UPROPERTY()
	ENavGen3DDrawMode DebugDrawMode = ENavGen3DDrawMode::None;

	UPROPERTY()
	bool DrawCameraVolume = false;

	UPROPERTY()
	bool DebugDrawConnections = false;

	UPROPERTY()
	bool DrawConnected = false;

	UPROPERTY()
	float DebugDrawTime = 0.0f;

	UPROPERTY()
	int32 DebugNavMeshAgentIndex = 0;

	UPROPERTY()
	float Epsilon = 0.1f;

	UPROPERTY()
	TArray<TObjectPtr<ANavGen3DBoundsVolume>> BoundsVolumes;

public:
	UWorld* FindWorld() const;

private:
	TWeakPtr<SNavGen3DWindow> NavGen3DWindowPtr;

	inline static uint64 NavMeshVolumeIDGenerator = 0;
	TMap<uint64, NavMeshVolume> NavMeshSolutionMap;
	TMap<uint64, TArray<uint64>> NavMeshSolutionMapByLocation;
	TArray<NavMeshVolume> ProcessVolumesList;
	TMap<uint64, uint64> NavMeshVolumeMap_X;
	TMap<uint64, uint64> NavMeshVolumeMap_Y;
	TMap<uint64, uint64> NavMeshVolumeMap_Z;
	TMap<int32, TMap<uint64, TArray<NavVolumeConnection>>> NavVolumeConnectionMap;
};
