// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DBoundsVolume.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DSettings.h"

ANavGen3DBoundsVolume::ANavGen3DBoundsVolume(const FObjectInitializer& InObjectInitializer)
	: Super(InObjectInitializer)
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

void ANavGen3DBoundsVolume::PostActorCreated()
{
	Super::PostActorCreated();
	const UNavGen3DSettings* Settings = GetDefault<UNavGen3DSettings>();
	MinVolumeSize = FMath::Max((float)Settings->DefaultMinVolumeSize, Settings->GeneratedMinVolumeSize);
}

void ANavGen3DBoundsVolume::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
	const float GeneratedMin = GetDefault<UNavGen3DSettings>()->GeneratedMinVolumeSize;
	MinVolumeSize = FMath::Max(MinVolumeSize, GeneratedMin);
}

void ANavGen3DBoundsVolume::GenerateNavMesh3D()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->InitializeNavMesh3D();
		Subsystem->GenerateNavMesh3DFromBoundsVolume(this);
		const int32 AgentCount = Subsystem->GetSupportedAgentCount();
		for (int32 AgentIndex = 0; AgentIndex < AgentCount; ++AgentIndex)
		{
			Subsystem->GenerateNavMesh3DConnections(AgentIndex);
		}
		Subsystem->PruneNavMesh3D();
	}
}

void ANavGen3DBoundsVolume::BeginPlay()
{
	Super::BeginPlay();
	Status = ENavGen3DBoundsVolumeStatus::InPlay;
}

void ANavGen3DBoundsVolume::EndPlay(const EEndPlayReason::Type InEndPlayReason)
{
	Status = ENavGen3DBoundsVolumeStatus::None;
	Super::EndPlay(InEndPlayReason);
}
