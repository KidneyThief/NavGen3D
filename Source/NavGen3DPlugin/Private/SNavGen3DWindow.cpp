// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNavGen3DWindow.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "GameFramework/PlayerController.h"
#include "Editor.h"
#include "Selection.h"
#include "DrawDebugHelpers.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Views/SListView.h"

void SNavGen3DWindow::Construct(const FArguments& InArgs)
{
	const FSlateColor LabelColor(FLinearColor(0.2f, 0.6f, 1.0f));
	const FSlateColor TextColor(FLinearColor::White);

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(620.0f)
		.MinDesiredHeight(520.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 4.0f, 4.0f, 4.0f)
			[
				SNew(SButton)
				.Text(FText::FromString("Clear Debug"))
				.OnClicked(this, &SNavGen3DWindow::OnClearDebugClicked)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SVerticalBox)
						.Clipping(EWidgetClipping::ClipToBounds)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 8.0f, 8.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString("Debug Settings"))
							.ColorAndOpacity(LabelColor)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 0.0f, 8.0f, 4.0f)
						[
							SNew(SSeparator)
							.Orientation(Orient_Horizontal)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString("Draw Time:  "))
								.ColorAndOpacity(TextColor)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(120.0f)
								.Value(this, &SNavGen3DWindow::GetDebugDrawTime)
								.OnValueChanged(this, &SNavGen3DWindow::OnDebugDrawTimeChanged)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(this, &SNavGen3DWindow::GetDebugDrawTimeText)
								.ColorAndOpacity(TextColor)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Top)
							.Padding(0.0f, 2.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(FText::FromString("Draw Mode:  "))
								.ColorAndOpacity(TextColor)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SCheckBox)
										.IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::None)
										.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::None)
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("None")).ColorAndOpacity(TextColor) ]
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SCheckBox)
										.IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::NavBounds3D)
										.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::NavBounds3D)
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("Nav Bounds 3D")).ColorAndOpacity(TextColor) ]
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SCheckBox)
										.IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::NavMesh3D)
										.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::NavMesh3D)
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("Nav Mesh 3D")).ColorAndOpacity(TextColor) ]
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(SCheckBox)
										.IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::UnprocessedVolumes)
										.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::UnprocessedVolumes)
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("Unprocessed Volumes")).ColorAndOpacity(TextColor) ]
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString("Draw Camera Volume:  "))
								.ColorAndOpacity(TextColor)
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SNavGen3DWindow::GetDrawCameraVolumeState)
								.OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawCameraVolumeChanged)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 8.0f, 8.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString("Generation"))
							.ColorAndOpacity(LabelColor)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 0.0f, 8.0f, 4.0f)
						[
							SNew(SSeparator)
							.Orientation(Orient_Horizontal)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))

							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SButton)
								.Text(FText::FromString("Reset"))
								.OnClicked(this, &SNavGen3DWindow::OnResetNavMesh3DClicked)
							]

							+ SUniformGridPanel::Slot(0, 1)
							[
								SNew(SButton)
								.Text(FText::FromString("Validate Embedded Bounds Volumes"))
								.OnClicked(this, &SNavGen3DWindow::OnValidateEmbeddedBoundsVolumesClicked)
							]

							+ SUniformGridPanel::Slot(0, 2)
							[
								SNew(SButton)
								.Text(FText::FromString("Generate NavMesh 3D For Selection"))
								.OnClicked(this, &SNavGen3DWindow::OnGenerateNavMesh3DForSelectionClicked)
							]

							+ SUniformGridPanel::Slot(0, 3)
							[
								SNew(SButton)
								.Text(FText::FromString("Validate NavMesh3D"))
								.OnClicked(this, &SNavGen3DWindow::OnValidateNavMesh3DClicked)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 8.0f, 8.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString("Gen Iteration (Debug)"))
							.ColorAndOpacity(LabelColor)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 0.0f, 8.0f, 4.0f)
						[
							SNew(SSeparator)
							.Orientation(Orient_Horizontal)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))

							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SButton)
								.Text(FText::FromString("Process Camera Volume"))
								.OnClicked(this, &SNavGen3DWindow::OnProcessCameraVolumeClicked)
							]

							+ SUniformGridPanel::Slot(0, 1)
							[
								SNew(SButton)
								.Text(FText::FromString("Validate Camera Volume"))
								.OnClicked(this, &SNavGen3DWindow::OnValidateCameraVolumeClicked)
							]
						]

					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 8.0f, 8.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString("Stats"))
							.ColorAndOpacity(LabelColor)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 0.0f, 8.0f, 4.0f)
						[
							SNew(SSeparator)
							.Orientation(Orient_Horizontal)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Bounds Volumes:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetBoundsVolumeCountText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Unprocessed Volumes:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetUnprocessedVolumeCountText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Solution Volumes:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetSolutionVolumeCountText).ColorAndOpacity(TextColor) ]
						]
					]
				]

				+ SSplitter::Slot()
				.Value(0.5f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 8.0f, 4.0f, 4.0f)
					[
						SNew(SButton)
						.Text(FText::FromString("Clear"))
						.OnClicked(this, &SNavGen3DWindow::OnClearLogClicked)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 0.0f, 4.0f, 4.0f)
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
			]
		]
	];
}

void SNavGen3DWindow::AddLogEntry(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message)
{
	LogEntries.Add(MakeShared<FNavGen3DLogEntry>(Category, ActorName, Message));
	RefreshFilteredLog();
}

float SNavGen3DWindow::GetDebugDrawTime() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DebugDrawTime;
	}
	return 0.0f;
}

FText SNavGen3DWindow::GetDebugDrawTimeText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return FText::FromString(FString::Printf(TEXT("%.1fs"), Subsystem->DebugDrawTime));
	}
	return FText::FromString(TEXT("0.0s"));
}

void SNavGen3DWindow::OnDebugDrawTimeChanged(float NewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugDrawTime = NewValue;
	}
}

FReply SNavGen3DWindow::OnClearDebugClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (UWorld* World = Subsystem->FindWorld())
		{
			FlushPersistentDebugLines(World);
		}
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnClearLogClicked()
{
	LogEntries.Reset();
	RefreshFilteredLog();
	return FReply::Handled();
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

ECheckBoxState SNavGen3DWindow::GetDrawModeRadioState(ENavGen3DDrawMode Mode) const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DebugDrawMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return Mode == ENavGen3DDrawMode::None ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawModeRadioChanged(ECheckBoxState NewState, ENavGen3DDrawMode Mode)
{
	if (NewState == ECheckBoxState::Checked)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->DebugDrawMode = Mode;
		}
	}
}

ECheckBoxState SNavGen3DWindow::GetDrawCameraVolumeState() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DrawCameraVolume ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawCameraVolumeChanged(ECheckBoxState NewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DrawCameraVolume = (NewState == ECheckBoxState::Checked);
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

FText SNavGen3DWindow::GetBoundsVolumeCountText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		int32 Count = 0;
		for (const TObjectPtr<ANavGen3DBoundsVolume>& Volume : Subsystem->GetBoundsVolumes())
		{
			if (Volume && Volume->Enabled && !Volume->Embedded)
			{
				++Count;
			}
		}
		return FText::AsNumber(Count);
	}
	return FText::AsNumber(0);
}

FText SNavGen3DWindow::GetUnprocessedVolumeCountText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return FText::AsNumber(Subsystem->GetProcessVolumesCount());
	}
	return FText::AsNumber(0);
}

FText SNavGen3DWindow::GetSolutionVolumeCountText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return FText::AsNumber(Subsystem->GetSolutionVolumesCount());
	}
	return FText::AsNumber(0);
}

FReply SNavGen3DWindow::OnResetNavMesh3DClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->InitializeNavMesh3D();
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnProcessCameraVolumeClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		const FVector CameraLocation = Subsystem->GetCameraLocation();

		TOptional<NavMeshVolume> GenVolume = Subsystem->FindGenerationVolumeContainingLocation(CameraLocation, true);
		if (GenVolume.IsSet())
		{
			Subsystem->ProcessNavMeshVolume(GenVolume.GetValue());
		}
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnValidateCameraVolumeClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		const FVector CameraLocation = Subsystem->GetCameraLocation();
		NavMeshVolume* Volume = Subsystem->FindVolumeContainingLocation(CameraLocation);
		Subsystem->ValidateNavMesh3D(Volume);
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnValidateNavMesh3DClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->ValidateNavMesh3D();
	}
	return FReply::Handled();
}
