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
	int32 DefaultMinVolumeSize = 100;
};
