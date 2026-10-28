// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavMeshVolumeBase.h"

// Axis aligned volume that can additionally be clipped by a single plane, for
// geometry that intersects the volume at an angle.
struct NavMesh3DVolume : public NavMeshVolumeBase
{
	virtual NavMeshVolumeBase* Clone() const override { return new NavMesh3DVolume(*this); }
	virtual NavMesh3DVolume* AsNavMesh3DVolume() override { return this; }
	virtual const NavMesh3DVolume* AsNavMesh3DVolume() const override { return this; }

	// World space point on the clip plane. Stored independently of the bounds center so
	// the clipped-away portion may include the center.
	FVector ClipPoint = FVector::ZeroVector;

	// Perpendicular to the clip plane, pointing toward the clipped-away side.
	// ZeroVector means the volume is not clipped.
	FVector ClipVector = FVector::ZeroVector;

	bool IsClipped() const { return !ClipVector.IsNearlyZero(); }

	virtual bool IsInsideOrOn(const FVector& InPoint) const override
	{
		if (!Bounds.IsInsideOrOn(InPoint))
		{
			return false;
		}
		return FVector::DotProduct(InPoint - ClipPoint, ClipVector) <= 0.0f;
	}
};
