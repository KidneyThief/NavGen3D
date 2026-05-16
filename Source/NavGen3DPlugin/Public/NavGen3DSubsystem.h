// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DBoundsVolumeAsset.h"
#include "NavGen3DLog.h"
#include "NavMeshVolume.h"
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
class UNavGen3DSubsystem;

enum class EPathSearchStatus : uint8
{
	None        = 0,
	Initialized = 1,
	InProgress  = 2,
	Success     = 3,
	Fail        = 4,
};

struct FPathSearchNode
{
	uint64 VolumeID = 0;
	float DistFromSource = 0.0f;
	float EstDistToTarget = 0.0f;
	FPathSearchNode* PrevSearchSpace = nullptr;
};

struct FPathSearchSpace
{
	int32 AgentIndex = -1;
	uint64 OriginID = 0;
	uint64 DestinationID = 0;
	FVector Origin = FVector::ZeroVector;
	FVector Destination = FVector::ZeroVector;
	TArray<FVector> PathSolution;
	TArray<uint64> PathVolumeIDs;
	TArray<int32> PathConnectionAxes;
	TArray<bool> bIsIntermediate;
	TArray<FPathSearchNode> NodeHeap;
	EPathSearchStatus Status = EPathSearchStatus::None;

	void Reset();
	void Initialize(UNavGen3DSubsystem* InSubsystem, int32 InAgentIndex, const FVector& PathOrigin, const FVector& PathDestination);
	void DebugDrawPath(float InDrawTime) const;
};

UCLASS()
class NAVGEN3DPLUGIN_API UNavGen3DSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& InCollection) override;
	virtual void Deinitialize() override;

	void OnEndFrame();

	TArray<ANavGen3DBoundsVolume*> GetBoundsVolumes();
	void AddBoundsVolume(ANavGen3DBoundsVolume* InVolume);
	void RemoveBoundsVolume(ANavGen3DBoundsVolume* InVolume);
	void InitializeNavMesh3D();
	bool GenerateNavMesh3DFromBoundsVolume(ANavGen3DBoundsVolume* InVolume);
	bool GenerateNavMesh3D(NavMeshVolume* InVolume = nullptr);
	bool PlaneTrace(FVector InMin, FVector InMax, EAxis::Type InAxis, FVector& OutImpactPoint, bool& OutStartPenetrating);
	bool ValidateConnectionCollision(const FCollisionShape& InSphere, const FVector& InStart, const FVector& InEnd);
	bool AddNavMeshVolume(NavMeshVolume& RefVolume);
	void RemoveNavMeshVolume(uint64 InID);
	bool ProcessNavMeshVolume(NavMeshVolume& RefVolume, bool InDrawDebug = false);
	bool ProcessNavMeshVolumeInterior(NavMeshVolume& RefVolume, bool InDrawDebug = false);
	void SplitAlongLongestAxis(NavMeshVolume& RefVolume, bool InDrawDebug, const FString& InActorName);
	void ValidateEmbeddedBoundsVolumes();
	bool ValidateNavMesh3D(NavMeshVolume* InVolume = nullptr);
	void PruneNavMesh3D();
	void DetermineConnectivity();
	void SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> InWindow);
	void AddLogMessage(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage);

	bool IsPlayMode() const;
	FVector GetCameraLocation(int32 InAgentIndex = -1) const;
	bool GetAgentSettings(int32 InIndex, float& OutRadius, float& OutHeight) const;
	float GetAgentCollisionRadius(int32 InAgentIndex, bool bPadded) const;
	int32 GetSupportedAgentCount() const;
	bool GenerateNavMesh3DConnections(int32 InAgentIndex);
	NavMeshVolume* FindNavMeshVolumeByID(uint64 InID);
	void FindNavMeshVolumeConnections(int32 InAgentIndex, const FCollisionShape& InSphere, const NavMeshVolume& InSourceVolume);
	bool FindNavMeshVolumeConnection(int32 InAgentIndex, const FCollisionShape& InSphere, const NavMeshVolume& InSourceVolume, const NavMeshVolume& InNeighborVolume, int32 InAxis, FVector& OutLocation, int32& OutConnectionAxis);
	const TArray<FNavVolumeConnection>* GetVolumeConnections(int32 InAgentIndex, uint64 InVolumeID) const;

	static inline FString FVectorToString(const FVector& InVec)
	{
		return FString::Printf(TEXT("%.2f, %.2f, %.2f"), InVec.X, InVec.Y, InVec.Z);
	}
	inline static FVector InvalidLocation = FVector(FLT_MAX);
	inline static uint64 NavMeshAssetVersion = 1;
	int32 GetProcessVolumesCount() const { return ProcessVolumesList.Num(); }
	int32 GetSolutionVolumesCount() const { return NavMeshSolutionMap.Num(); }
	const TMap<uint64, NavMeshVolume>& GetNavMeshSolutionMap() const { return NavMeshSolutionMap; }
	const TMap<int32, TMap<uint64, TArray<FNavVolumeConnection>>>& GetNavVolumeConnectionMap() const { return NavVolumeConnectionMap; }
	void AddGeneratedVolumeToSolution(const FNavGen3DGeneratedVolume& InGeneratedVolume, ANavGen3DBoundsVolume* InParent);
	uint64 CalculateHash3D(const FVector& InVec) const;
	NavMeshVolume* FindVolumeContainingLocation(const FVector& InLocation);
	NavMeshVolume* FindClosestVolumeContainingLocation(int32 InAgentIndex, const FVector& InLocation, int32 InConnectivityID = 0, bool bPaddedRadius = true);
	bool PathFind(FPathSearchSpace& InSearchSpace);
	void FindPathPostProcess(FPathSearchSpace& InSearchSpace);
	void DebugFindPath(FVector InOrigin = InvalidLocation, FVector InDestination = InvalidLocation);
	bool DebugValidatePath(const FPathSearchSpace& InSearchSpace);
	TOptional<NavMeshVolume> FindGenerationVolumeContainingLocation(const FVector& InLocation, bool InRemoveFromProcessing);
	TOptional<NavMeshVolume> FindClosestGenerationVolume(const FVector& InLocation, bool InRemoveFromProcessing);
	TOptional<NavMeshVolume> PopNextProcessingVolume();
	float GetProcessMinVolumeSize(const FBox& InBounds, const TWeakObjectPtr<ANavGen3DBoundsVolume>& InParent) const;
	void DebugProcessSingleVolume(NavMeshVolume& InVolume, bool bDrawDebug);
	void DebugUndoLastProcess();
	bool HasDebugUndoState() const { return DebugUndoState.bIsValid; }

	UPROPERTY()
	ENavGen3DDrawMode DebugDrawMode = ENavGen3DDrawMode::None;

	UPROPERTY()
	bool DrawCameraVolume = false;

	UPROPERTY()
	bool DebugDrawConnections = false;

	UPROPERTY()
	bool DrawConnectivity = false;

	UPROPERTY()
	bool PathSmoothingEnabled = false;

	UPROPERTY()
	bool DebugRepathContinuous = false;

	UPROPERTY()
	float DebugRepathFrequency = 1.0f;

	float DebugRepathTimer = 0.0f;

	uint64 DrawVolumeID = 0;

	UPROPERTY()
	int32 DebugLevel = 0;

	bool bCameraLocationValidForAgent = false;

	UPROPERTY()
	float DebugDrawTime = 0.0f;

	UPROPERTY()
	int32 DebugNavMeshAgentIndex = 0;

	UPROPERTY()
	float Epsilon = 0.1f;

	UPROPERTY()
	FVector DebugPathOrigin = InvalidLocation;

	UPROPERTY()
	FVector DebugPathDestination = InvalidLocation;

	FPathSearchSpace DebugPathSearchSpace;

	TArray<TWeakObjectPtr<ANavGen3DBoundsVolume>> BoundsVolumes;

public:
	UWorld* FindWorld() const;

#if WITH_EDITOR
private:
	void OnMapChanged(uint32 InMapChangeFlags);
	void OnBeginPIE(bool bIsSimulating);
	void OnEndPIE(bool bIsSimulating);
#endif

private:
	struct FDebugProcessUndoState
	{
		NavMeshVolume ProcessedVolume;
		TMap<uint64, NavMeshVolume> SolutionMapSnapshot;
		TMap<uint64, TArray<uint64>> SolutionByLocationSnapshot;
		TMap<uint64, uint64> VolumeMap_X_Snapshot;
		TMap<uint64, uint64> VolumeMap_Y_Snapshot;
		TMap<uint64, uint64> VolumeMap_Z_Snapshot;
		uint64 IDGeneratorSnapshot = 0;
		int32 ProcessListCountBefore = 0;
		bool bIsValid = false;
	};
	FDebugProcessUndoState DebugUndoState;

	TWeakPtr<SNavGen3DWindow> NavGen3DWindowPtr;

	inline static uint64 NavMeshVolumeIDGenerator = 0;
	TMap<uint64, NavMeshVolume> NavMeshSolutionMap;
	TMap<uint64, TArray<uint64>> NavMeshSolutionMapByLocation;
	TArray<NavMeshVolume> ProcessVolumesList;
	TMap<uint64, uint64> NavMeshVolumeMap_X;
	TMap<uint64, uint64> NavMeshVolumeMap_Y;
	TMap<uint64, uint64> NavMeshVolumeMap_Z;
	TMap<int32, TMap<uint64, TArray<FNavVolumeConnection>>> NavVolumeConnectionMap;
};
