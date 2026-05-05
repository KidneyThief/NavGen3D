// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "Misc/CoreDelegates.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

void UNavGen3DSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FCoreDelegates::OnEndFrame.AddUObject(this, &UNavGen3DSubsystem::OnEndFrame);
}

void UNavGen3DSubsystem::Deinitialize()
{
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	Super::Deinitialize();
}

void UNavGen3DSubsystem::AddBoundsVolume(ANavGen3DBoundsVolume* Volume)
{
	BoundsVolumes.AddUnique(Volume);
}

void UNavGen3DSubsystem::RemoveBoundsVolume(ANavGen3DBoundsVolume* Volume)
{
	BoundsVolumes.Remove(Volume);
}

TArray<TObjectPtr<ANavGen3DBoundsVolume>> UNavGen3DSubsystem::GetBoundsVolumes()
{
	if (IsPlayMode())
	{
		return BoundsVolumes;
	}

	TArray<TObjectPtr<ANavGen3DBoundsVolume>> Result;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!World)
		{
			continue;
		}

		for (TActorIterator<ANavGen3DBoundsVolume> It(World); It; ++It)
		{
			Result.Add(*It);
		}
	}
	return Result;
}

void UNavGen3DSubsystem::OnEndFrame()
{
	if (!DrawNavBounds3D)
	{
		return;
	}

	for (TObjectPtr<ANavGen3DBoundsVolume> Volume : GetBoundsVolumes())
	{
		if (!Volume)
		{
			continue;
		}

		if (IsPlayMode() && Volume->Status != ENavGen3DBoundsVolumeStatus::InPlay)
		{
			continue;
		}

		FVector Origin;
		FVector Extent;
		Volume->GetActorBounds(false, Origin, Extent);
		DrawDebugBox(Volume->GetWorld(), Origin, Extent, Volume->GetActorRotation().Quaternion(), FColor::Purple, false, -1.0f, 0, 5.0f);
	}
}
