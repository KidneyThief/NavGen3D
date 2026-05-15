// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NavGen3DBoundsVolumeAsset.generated.h"

// Serializable mirror of FNavVolumeConnection (plain struct, not UHT-processed)
USTRUCT()
struct FNavGen3DStoredConnection
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 ID = 0;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	int32 ConnectionAxis = 0;
};

USTRUCT()
struct FNavGen3DVolumeConnectionList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FNavGen3DStoredConnection> Connections;
};

USTRUCT()
struct FNavGen3DGeneratedVolume
{
	GENERATED_BODY()

	UPROPERTY()
	uint64 Version = 0;

	UPROPERTY()
	uint64 ID = 0;

	UPROPERTY()
	FVector BoundsMin = FVector::ZeroVector;

	UPROPERTY()
	FVector BoundsMax = FVector::ZeroVector;

	// Indexed by agent index — replaces the TMap<int32,int32> on NavMeshVolume
	UPROPERTY()
	TArray<int32> ConnectivityIDs;

	UPROPERTY()
	TMap<int32, FNavGen3DVolumeConnectionList> Connections;
};

UCLASS(BlueprintType)
class NAVGEN3DPLUGIN_API UNavGen3DBoundsVolumeAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	uint64 SavedVersion = 0;

	UPROPERTY()
	FVector SavedBoundsOrigin = FVector::ZeroVector;

	UPROPERTY()
	FVector SavedBoundsExtent = FVector::ZeroVector;

	UPROPERTY()
	TArray<FNavGen3DGeneratedVolume> GeneratedVolumes;
};
