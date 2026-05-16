// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NavGen3DSettings.generated.h"

UCLASS(config=NavGen3D, defaultconfig, meta=(DisplayName="NavGen3D"))
class NAVGEN3DPLUGIN_API UNavGen3DSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNavGen3DSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(config, EditAnywhere, Category="Generation")
	float GeneratedMinVolumeSize = 50.0f;

	UPROPERTY(config, EditAnywhere, Category="Generation")
	int32 DefaultMinVolumeSize = 100;

	UPROPERTY(config, EditAnywhere, Category="Generation")
	int32 MaxVolumeSize = 1000;

	UPROPERTY(config, EditAnywhere, Category="Generation")
	int32 MaxVolumeCount = 10000;
};

#define NavGen3D_DISABLE_OPTIMIZATION UE_DISABLE_OPTIMIZATION
#define NavGen3D_ENABLE_OPTIMIZATION  UE_ENABLE_OPTIMIZATION
