// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavMeshVolumeBase.h"

struct FNavVolumeConnection
{
	uint64 ID = 0;
	FVector Location = FVector::ZeroVector;
	int32 ConnectionAxis = 0;
};

struct NavMeshVolume_AA : public NavMeshVolumeBase
{
	virtual NavMeshVolumeBase* Clone() const override { return new NavMeshVolume_AA(*this); }
};
