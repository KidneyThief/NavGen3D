// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ANavGen3DBoundsVolume;
struct NavMesh3DVolume;

// Volumes are heap allocated and owned by UNavGen3DSubsystem's containers; they are
// created with new and destroyed with delete, never garbage collected.
struct NavMeshVolumeBase
{
	virtual ~NavMeshVolumeBase() = default;

	// Heap-allocated copy of the same derived type; the caller owns the result.
	virtual NavMeshVolumeBase* Clone() const = 0;

	virtual bool IsInsideOrOn(const FVector& InPoint) const { return Bounds.IsInsideOrOn(InPoint); }

	// RTTI is disabled in UE, so derived types identify themselves through these
	virtual NavMesh3DVolume* AsNavMesh3DVolume() { return nullptr; }
	virtual const NavMesh3DVolume* AsNavMesh3DVolume() const { return nullptr; }

	uint64 ID = 0;
	FBox Bounds;
	TWeakObjectPtr<ANavGen3DBoundsVolume> ParentBoundsVolume;
	uint64 Next_X = 0;
	uint64 Next_Y = 0;
	uint64 Next_Z = 0;
	TMap<int32, int32> ConnectivityIDByAgent;
};
