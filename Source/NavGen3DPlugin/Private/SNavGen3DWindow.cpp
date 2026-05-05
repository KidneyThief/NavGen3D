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
		SNew(SBox)
		.Padding(8.0f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SNavGen3DWindow::GetDrawNavMesh3DState)
			.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawNavMesh3DChanged)
			[
				SNew(STextBlock)
				.Text(FText::FromString("Draw NavMesh 3D"))
			]
		]
	];
}

ECheckBoxState SNavGen3DWindow::GetDrawNavMesh3DState() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DrawNavMesh3D ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawNavMesh3DChanged(ECheckBoxState NewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DrawNavMesh3D = (NewState == ECheckBoxState::Checked);
	}
}
