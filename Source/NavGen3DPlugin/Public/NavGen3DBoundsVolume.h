// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "NavGen3DBoundsVolume.generated.h"

UCLASS()
class NAVGEN3DPLUGIN_API ANavGen3DBoundsVolume : public AVolume
{
	GENERATED_BODY()

public:
	ANavGen3DBoundsVolume(const FObjectInitializer& ObjectInitializer);
};
