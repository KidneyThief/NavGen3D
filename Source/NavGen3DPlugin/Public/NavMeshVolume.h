// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ANavGen3DBoundsVolume;

struct NavMeshVolume
{
	uint64 ID = 0;
	FBox Bounds;
	TWeakObjectPtr<ANavGen3DBoundsVolume> ParentBoundsVolume;
};
