// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SNavGen3DWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNavGen3DWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	ECheckBoxState GetDrawNavBounds3DState() const;
	void OnDrawNavBounds3DChanged(ECheckBoxState NewState);
};
