// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DSubsystem.generated.h"

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

	bool IsPlayMode()
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			const UWorld* World = WorldContext.World();
			if (World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
			{
				return true;
			}
		}
		return false;
	}

	UPROPERTY()
	bool DrawNavBounds3D = false;

	UPROPERTY()
	TArray<TObjectPtr<ANavGen3DBoundsVolume>> BoundsVolumes;
};
