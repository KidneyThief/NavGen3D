// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "NavGen3DBoundsVolume.h"
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

UCLASS()
class NAVGEN3DPLUGIN_API UNavGen3DSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void OnEndFrame();

	TArray<TObjectPtr<ANavGen3DBoundsVolume>> GetBoundsVolumes();
	void AddBoundsVolume(ANavGen3DBoundsVolume* Volume);
	void RemoveBoundsVolume(ANavGen3DBoundsVolume* Volume);
	void InitializeNavMesh3D();
	bool GenerateNavMesh3DFromBoundsVolume(ANavGen3DBoundsVolume* Volume);
	bool GenerateNavMesh3D(NavMeshVolume* Volume = nullptr);
	bool PlaneTrace(FVector Min, FVector Max, EAxis::Type Axis, FVector& OutImpactPoint);
	bool AddNavMeshVolume(NavMeshVolume& Volume);
	void RemoveNavMeshVolume(uint64 ID);
	bool ProcessNavMeshVolume(NavMeshVolume& Volume);
	void ValidateEmbeddedBoundsVolumes();
	bool ValidateNavMesh3D(NavMeshVolume* Volume = nullptr);
	void SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> Window);
	void AddLogMessage(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message);

	bool IsPlayMode();
	FVector GetCameraLocation();
	int32 GetProcessVolumesCount() const { return ProcessVolumesList.Num(); }
	int32 GetSolutionVolumesCount() const { return NavMeshSolutionMap.Num(); }
	uint64 CalculateHash3D(const FVector& Vec) const;
	NavMeshVolume* FindVolumeContainingLocation(const FVector& Location);
	TOptional<NavMeshVolume> FindGenerationVolumeContainingLocation(const FVector& Location, bool bRemoveFromProcessing);

	UPROPERTY()
	ENavGen3DDrawMode DebugDrawMode = ENavGen3DDrawMode::None;

	UPROPERTY()
	bool DrawCameraVolume = false;

	UPROPERTY()
	float DebugDrawTime = 0.0f;

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
};
