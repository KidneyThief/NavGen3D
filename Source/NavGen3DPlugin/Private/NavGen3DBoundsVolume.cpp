// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DBoundsVolume.h"
#include "NavGen3DSubsystem.h"

ANavGen3DBoundsVolume::ANavGen3DBoundsVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && GEngine)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->AddBoundsVolume(this);
		}
	}
}

ANavGen3DBoundsVolume::~ANavGen3DBoundsVolume()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && GEngine)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->RemoveBoundsVolume(this);
		}
	}
}

void ANavGen3DBoundsVolume::BeginPlay()
{
	Super::BeginPlay();
	Status = ENavGen3DBoundsVolumeStatus::InPlay;
}

void ANavGen3DBoundsVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Status = ENavGen3DBoundsVolumeStatus::Loaded;
	Super::EndPlay(EndPlayReason);
}
