// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DLog.h"
#include "NavGen3DSubsystem.generated.h"

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
	bool GenerateNavMesh3D(ANavGen3DBoundsVolume* Volume);
	void ValidateEmbeddedBoundsVolumes();
	void SetNavGen3DWindow(TSharedPtr<SNavGen3DWindow> Window);
	void AddLogMessage(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message);

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

private:
	TWeakPtr<SNavGen3DWindow> NavGen3DWindowPtr;
};
