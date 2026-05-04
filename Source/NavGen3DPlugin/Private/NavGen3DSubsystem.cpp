// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DSubsystem.h"
#include "Misc/CoreDelegates.h"

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

void UNavGen3DSubsystem::OnEndFrame()
{
	UE_LOG(LogTemp, Display, TEXT("ticking..."));
}
