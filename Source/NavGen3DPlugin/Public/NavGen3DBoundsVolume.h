// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "NavGen3DBoundsVolume.generated.h"

UENUM(BlueprintType)
enum class ENavGen3DBoundsVolumeStatus : uint8
{
	None = 0,
	Unloaded,
	Streaming,
	Loaded,
	InPlay
};

UCLASS()
class NAVGEN3DPLUGIN_API ANavGen3DBoundsVolume : public AVolume
{
	GENERATED_BODY()

public:
	ANavGen3DBoundsVolume(const FObjectInitializer& InObjectInitializer);
	virtual ~ANavGen3DBoundsVolume() override;

	virtual void PostActorCreated() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type InEndPlayReason) override;

	UFUNCTION(CallInEditor, Category = "NavGen3D")
	void GenerateNavMesh3D();

	UPROPERTY(BlueprintReadOnly, Category = "NavGen3D")
	ENavGen3DBoundsVolumeStatus Status = ENavGen3DBoundsVolumeStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NavGen3D")
	bool Enabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "NavGen3D")
	bool Embedded = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NavGen3D")
	float MinVolumeSize = 0.0f;
};
