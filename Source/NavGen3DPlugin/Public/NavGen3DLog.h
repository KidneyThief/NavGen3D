// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class ENavGen3DLogCategory : uint8
{
	Info,
	Warning,
	Error
};

struct FNavGen3DLogEntry
{
	ENavGen3DLogCategory Category;
	FString ActorName;
	FString Message;

	FNavGen3DLogEntry(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage)
		: Category(InCategory), ActorName(InActorName), Message(InMessage)
	{}
};
