// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "NavGen3DBoundsVolume.generated.h"

UENUM(BlueprintType)
enum class ENavGen3DBoundsVolumeStatus : uint8
{
	Unloaded,
	Loading,
	Loaded,
	InPlay
};

UCLASS()
class NAVGEN3DPLUGIN_API ANavGen3DBoundsVolume : public AVolume
{
	GENERATED_BODY()

public:
	ANavGen3DBoundsVolume(const FObjectInitializer& ObjectInitializer);
	virtual ~ANavGen3DBoundsVolume() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintReadOnly, Category = "NavGen3D")
	ENavGen3DBoundsVolumeStatus Status = ENavGen3DBoundsVolumeStatus::Unloaded;
};
