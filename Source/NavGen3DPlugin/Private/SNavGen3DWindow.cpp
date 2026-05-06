// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNavGen3DWindow.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "Editor.h"
#include "Selection.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Views/SListView.h"

void SNavGen3DWindow::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		+ SSplitter::Slot()
		.Value(0.5f)
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
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Validate Embedded Bounds Volumes"))
				.OnClicked(this, &SNavGen3DWindow::OnValidateEmbeddedBoundsVolumesClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f, 4.0f, 8.0f, 4.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Generate NavMesh 3D For Selection"))
				.OnClicked(this, &SNavGen3DWindow::OnGenerateNavMesh3DForSelectionClicked)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
		]

		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 8.0f, 4.0f, 4.0f)
			[
				SNew(SSearchBox)
				.OnTextChanged(this, &SNavGen3DWindow::OnSearchTextChanged)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(4.0f, 0.0f, 4.0f, 4.0f)
			[
				SAssignNew(LogListView, SListView<TSharedPtr<FNavGen3DLogEntry>>)
				.ListItemsSource(&FilteredLogEntries)
				.OnGenerateRow(this, &SNavGen3DWindow::GenerateLogRow)
			]
		]
	];
}

void SNavGen3DWindow::AddLogEntry(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message)
{
	LogEntries.Add(MakeShared<FNavGen3DLogEntry>(Category, ActorName, Message));
	RefreshFilteredLog();
}

void SNavGen3DWindow::OnSearchTextChanged(const FText& InText)
{
	SearchText = InText.ToString();
	RefreshFilteredLog();
}

void SNavGen3DWindow::RefreshFilteredLog()
{
	FilteredLogEntries.Reset();
	for (const TSharedPtr<FNavGen3DLogEntry>& Entry : LogEntries)
	{
		if (SearchText.IsEmpty() || Entry->Message.Contains(SearchText))
		{
			FilteredLogEntries.Add(Entry);
		}
	}
	if (LogListView.IsValid())
	{
		LogListView->RequestListRefresh();
		LogListView->ScrollToBottom();
	}
}

TSharedRef<ITableRow> SNavGen3DWindow::GenerateLogRow(TSharedPtr<FNavGen3DLogEntry> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FNavGen3DLogEntry>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->Message))
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

FReply SNavGen3DWindow::OnValidateEmbeddedBoundsVolumesClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->ValidateEmbeddedBoundsVolumes();
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnGenerateNavMesh3DForSelectionClicked()
{
	if (GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (ANavGen3DBoundsVolume* Volume = Cast<ANavGen3DBoundsVolume>(*It))
			{
				Volume->GenerateNavMesh3D();
			}
		}
	}
	return FReply::Handled();
}
