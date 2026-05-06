// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "SNavGen3DWindow.h"
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

void UNavGen3DSubsystem::SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> Window)
{
	NavGen3DWindowPtr = Window;
}

void UNavGen3DSubsystem::AddLogMessage(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message)
{
	static const TMap<ENavGen3DLogCategory, FString> CategoryLabels = {
		{ ENavGen3DLogCategory::Info,    TEXT("Info")    },
		{ ENavGen3DLogCategory::Warning, TEXT("Warning") },
		{ ENavGen3DLogCategory::Error,   TEXT("Error")   }
	};
	const FString ResolvedName = ActorName.IsEmpty() ? TEXT("<unknown>") : ActorName;
	const FString FormattedMessage = FString::Printf(TEXT("[%s] %s: %s"), *CategoryLabels[Category], *ResolvedName, *Message);
	if (TSharedPtr<SNavGen3DWindow> Window = NavGen3DWindowPtr.Pin())
	{
		Window->AddLogEntry(Category, ResolvedName, FormattedMessage);
	}
}

bool UNavGen3DSubsystem::GenerateNavMesh3D(ANavGen3DBoundsVolume* Volume)
{
	AddLogMessage(ENavGen3DLogCategory::Info, Volume->GetActorLabel(), TEXT("generating NavMesh3D..."));
	return false;
}

void UNavGen3DSubsystem::ValidateEmbeddedBoundsVolumes()
{
	if (IsPlayMode())
	{
		return;
	}

	TArray<TObjectPtr<ANavGen3DBoundsVolume>> Volumes = GetBoundsVolumes();

	for (ANavGen3DBoundsVolume* Volume : Volumes)
	{
		if (!Volume || !Volume->Enabled)
		{
			continue;
		}

		Volume->Embedded = false;
		const FBox VolumeBox = Volume->GetComponentsBoundingBox();

		for (ANavGen3DBoundsVolume* OtherVolume : Volumes)
		{
			if (!OtherVolume || !OtherVolume->Enabled || OtherVolume == Volume)
			{
				continue;
			}

			const FBox OtherBox = OtherVolume->GetComponentsBoundingBox();
			const bool bIsContained = OtherBox.IsInside(VolumeBox);
			const bool bOverlapsAndSmaller = VolumeBox.Intersect(OtherBox) && VolumeBox.GetVolume() < OtherBox.GetVolume();

			if (bIsContained || bOverlapsAndSmaller)
			{
				Volume->Embedded = true;
				break;
			}
		}
	}
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
		if (!Volume || !Volume->Enabled)
		{
			continue;
		}

		if (IsPlayMode() && Volume->Status != ENavGen3DBoundsVolumeStatus::InPlay)
		{
			continue;
		}

		const FColor DrawColor = Volume->Embedded ? FColor::Cyan : FColor::Purple;
		FVector Origin;
		FVector Extent;
		Volume->GetActorBounds(false, Origin, Extent);
		DrawDebugBox(Volume->GetWorld(), Origin, Extent, Volume->GetActorRotation().Quaternion(), DrawColor, false, -1.0f, 0, 5.0f);
	}
}
