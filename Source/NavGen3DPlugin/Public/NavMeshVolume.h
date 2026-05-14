// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ANavGen3DBoundsVolume;

struct NavMeshVolume
{
	uint64 ID = 0;
	FBox Bounds;
	TWeakObjectPtr<ANavGen3DBoundsVolume> ParentBoundsVolume;
	uint64 Next_X = 0;
	uint64 Next_Y = 0;
	uint64 Next_Z = 0;
	TMap<int32, int32> ConnectivityIDByAgent;
};

struct NavVolumeConnection
{
	uint64 ID = 0;
	FVector Location = FVector::ZeroVector;
	int32 ConnectionAxis = 0;
};
