// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNavGen3DWindow.h"
#include "NavGen3DSubsystem.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

void SNavGen3DWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SNavGen3DWindow::GetDrawNavBounds3DState)
			.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawNavBounds3DChanged)
			[
				SNew(SBox)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString("Draw NavBounds3D"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
	];
}

ECheckBoxState SNavGen3DWindow::GetDrawNavBounds3DState() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DrawNavBounds3D ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawNavBounds3DChanged(ECheckBoxState NewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DrawNavBounds3D = (NewState == ECheckBoxState::Checked);
	}
}
