// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNavGen3DWindow.h"
#include "NavGen3DSubsystem.h"
#include "NavGen3DBoundsVolume.h"
#include "NavGen3DMover.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Editor.h"
#include "Selection.h"
#include "DrawDebugHelpers.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "NavGen3DSettings.h"

// -- defined in NavGen3DSettings.h
NavGen3D_DISABLE_OPTIMIZATION

static ANavGen3DMover* GetFirstMover(UNavGen3DSubsystem* Subsystem)
{
	if (UWorld* World = Subsystem ? Subsystem->FindWorld() : nullptr)
	{
		for (TActorIterator<ANavGen3DMover> It(World); It; ++It)
			return *It;
	}
	return nullptr;
}

static void ForEachMover(UNavGen3DSubsystem* Subsystem, TFunctionRef<void(ANavGen3DMover*)> Func)
{
	if (UWorld* World = Subsystem ? Subsystem->FindWorld() : nullptr)
	{
		for (TActorIterator<ANavGen3DMover> It(World); It; ++It)
			Func(*It);
	}
}

void SNavGen3DWindow::Construct(const FArguments& InArgs)
{
	const FSlateColor LabelColor(FLinearColor(0.2f, 0.6f, 1.0f));
	const FSlateColor TextColor(FLinearColor::White);

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(300.0f)
		.MinDesiredHeight(520.0f)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				// Generation status banner — lives outside any collapsible section
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 8.0f, 8.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(this, &SNavGen3DWindow::GetGenerationStatusText)
					.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.2f)))
					.Visibility(this, &SNavGen3DWindow::GetGenerationStatusVisibility)
				]

				// ---- Debug Settings ----
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString("Debug Settings"))
						.ColorAndOpacity(LabelColor)
					]
					.BodyContent()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SButton)
							.Text(FText::FromString("Clear Debug Lines"))
							.OnClicked(this, &SNavGen3DWindow::OnClearDebugClicked)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Camera Loc:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetCameraLocationText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("NavMesh Agent:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(GetNavMeshAgentSliderMax())
								.StepSize(1.0f)
								.Value(this, &SNavGen3DWindow::GetNavMeshAgentSliderValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnNavMeshAgentSliderChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetNavMeshAgentText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 2.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(FText::FromString("Draw Mode:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::None).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::None) ]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("None")).ColorAndOpacity(TextColor) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::NavBounds3D).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::NavBounds3D) ]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("Nav Bounds 3D")).ColorAndOpacity(TextColor) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::NavMesh3D).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::NavMesh3D) ]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[ SNew(STextBlock).Text(FText::FromString("Nav Mesh 3D")).ColorAndOpacity(TextColor) ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawModeRadioState, ENavGen3DDrawMode::UnprocessedVolumes).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawModeRadioChanged, ENavGen3DDrawMode::UnprocessedVolumes) ]
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
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Draw Connections:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawConnectionsState).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawConnectionsChanged) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Draw Connectivity:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawConnectivityState).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawConnectivityChanged) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Camera Vol:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetCameraVolumeText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Draw Camera Volume:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetDrawCameraVolumeState).OnCheckStateChanged(this, &SNavGen3DWindow::OnDrawCameraVolumeChanged) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Draw Volume By ID:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SEditableTextBox)
								.OnTextChanged(this, &SNavGen3DWindow::OnVolumeIDTextChanged)
								.OnTextCommitted(this, &SNavGen3DWindow::OnVolumeIDTextCommitted)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Debug Level:")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(4.0f).StepSize(1.0f)
								.Value(this, &SNavGen3DWindow::GetDebugLevel)
								.OnValueChanged(this, &SNavGen3DWindow::OnDebugLevelChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetDebugLevelText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Draw Time:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(120.0f)
								.Value(this, &SNavGen3DWindow::GetDebugDrawTime)
								.OnValueChanged(this, &SNavGen3DWindow::OnDebugDrawTimeChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetDebugDrawTimeText).ColorAndOpacity(TextColor) ]
						]
					]
				]

				// ---- Generation + Gen Iteration (Debug) + Volume Iteration (Debug) ----
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString("Generation"))
						.ColorAndOpacity(LabelColor)
					]
					.BodyContent()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
							+ SUniformGridPanel::Slot(0, 0)
							[ SNew(SButton).Text(FText::FromString("Reset")).OnClicked(this, &SNavGen3DWindow::OnResetNavMesh3DClicked).IsEnabled(this, &SNavGen3DWindow::IsNotPlayMode) ]
							+ SUniformGridPanel::Slot(0, 1)
							[ SNew(SButton).Text(FText::FromString("Generate NavMesh3D")).OnClicked(this, &SNavGen3DWindow::OnGenerateNavMesh3DForSelectionClicked).IsEnabled(this, &SNavGen3DWindow::IsGenerationEnabled) ]
							+ SUniformGridPanel::Slot(0, 2)
							[ SNew(SButton).Text(FText::FromString("Validate NavMesh3D")).OnClicked(this, &SNavGen3DWindow::OnValidateNavMesh3DClicked).IsEnabled(this, &SNavGen3DWindow::IsGenerationEnabled) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 2.0f)
						[ SNew(STextBlock).Text(FText::FromString("Gen Iteration (Debug)")).ColorAndOpacity(LabelColor) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 4.0f)
						[ SNew(SSeparator).Orientation(Orient_Horizontal) ]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
							+ SUniformGridPanel::Slot(0, 0)
							[ SNew(SButton).Text(FText::FromString("Generate Volumes")).OnClicked(this, &SNavGen3DWindow::OnGenerateVolumesClicked).IsEnabled(this, &SNavGen3DWindow::IsGenerationEnabled) ]
							+ SUniformGridPanel::Slot(0, 1)
							[ SNew(SButton).Text(FText::FromString("Find Connections")).OnClicked(this, &SNavGen3DWindow::OnFindConnectionsClicked).IsEnabled(this, &SNavGen3DWindow::IsNotPlayMode) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 2.0f)
						[ SNew(STextBlock).Text(FText::FromString("Volume Iteration (Debug)")).ColorAndOpacity(LabelColor) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 4.0f)
						[ SNew(SSeparator).Orientation(Orient_Horizontal) ]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
							.IsEnabled(this, &SNavGen3DWindow::IsGenerationEnabled)
							+ SUniformGridPanel::Slot(0, 0)
							[ SNew(SButton).Text(FText::FromString("Process Next Volume")).OnClicked(this, &SNavGen3DWindow::OnProcessNextVolumeClicked) ]
							+ SUniformGridPanel::Slot(0, 1)
							[ SNew(SButton).Text(FText::FromString("Process Remaining Volumes")).OnClicked(this, &SNavGen3DWindow::OnProcessRemainingVolumesClicked) ]
							+ SUniformGridPanel::Slot(0, 2)
							[ SNew(SButton).Text(FText::FromString("Process Camera Volume")).OnClicked(this, &SNavGen3DWindow::OnProcessCameraVolumeClicked) ]
							+ SUniformGridPanel::Slot(0, 3)
							[ SNew(SButton).Text(FText::FromString("Validate Camera Volume")).OnClicked(this, &SNavGen3DWindow::OnValidateCameraVolumeClicked) ]
							+ SUniformGridPanel::Slot(0, 4)
							[ SNew(SButton).Text(FText::FromString("Find Camera Volume Connections")).OnClicked(this, &SNavGen3DWindow::OnFindCameraVolumeConnectionsClicked) ]
							+ SUniformGridPanel::Slot(0, 5)
							[ SNew(SButton).Text(FText::FromString("Undo Last Process")).IsEnabled(this, &SNavGen3DWindow::IsUndoLastProcessEnabled).OnClicked(this, &SNavGen3DWindow::OnUndoLastProcessClicked) ]
						]
					]
				]

				// ---- Path Finding (Debug) ----
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString("Path Finding (Debug)"))
						.ColorAndOpacity(LabelColor)
					]
					.BodyContent()
					[
						SNew(SVerticalBox)
						.IsEnabled(this, &SNavGen3DWindow::IsPathFindingEnabled)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[ SNew(SButton).Text(FText::FromString("Reset")).OnClicked(this, &SNavGen3DWindow::OnResetPathClicked) ]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 2.0f)
						[
							SNew(STextBlock)
							.Text(this, &SNavGen3DWindow::GetPathOriginStatusText)
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.2f)))
							.Visibility(this, &SNavGen3DWindow::GetPathOriginStatusVisibility)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Path Smoothing:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(SCheckBox).IsChecked(this, &SNavGen3DWindow::GetPathSmoothingState).OnCheckStateChanged(this, &SNavGen3DWindow::OnPathSmoothingChanged) ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))

							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[ SNew(SButton).Text(FText::FromString("Set PathOrigin")).IsEnabled(this, &SNavGen3DWindow::IsCameraLocationValidForAgent).OnClicked(this, &SNavGen3DWindow::OnSetPathOriginClicked) ]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetDebugPathOriginText).ColorAndOpacity(TextColor) ]
							]

							+ SUniformGridPanel::Slot(0, 1)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[ SNew(SButton).Text(FText::FromString("Path To Camera")).IsEnabled(this, &SNavGen3DWindow::IsCameraLocationValidForAgent).OnClicked(this, &SNavGen3DWindow::OnPathToCameraClicked) ]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetDebugPathDestinationText).ColorAndOpacity(TextColor) ]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[ SNew(STextBlock).Text(FText::FromString("Continuous")).ColorAndOpacity(TextColor) ]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SCheckBox)
									.IsChecked_Lambda([]() -> ECheckBoxState {
										if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
											return Subsystem->DebugRepathContinuous ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
										return ECheckBoxState::Unchecked;
									})
									.OnCheckStateChanged(this, &SNavGen3DWindow::OnDebugRepathContinuousChanged)
								]
							]

							+ SUniformGridPanel::Slot(0, 2)
							[ SNew(SButton).Text(FText::FromString("Re-Path With Validate")).OnClicked(this, &SNavGen3DWindow::OnRePathClicked) ]
						]
					]
				]

				// ---- Mover 3D Settings ----
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString("Mover 3D Settings"))
						.ColorAndOpacity(LabelColor)
					]
					.BodyContent()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Agent Index:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f)
								.MaxValue(GetNavMeshAgentSliderMax())
								.StepSize(1.0f)
								.Value(this, &SNavGen3DWindow::GetMoverAgentIndexValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnMoverAgentIndexChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetMoverAgentIndexText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Max Velocity:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(3000.0f)
								.Value(this, &SNavGen3DWindow::GetMoverMaxVelocityValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnMoverMaxVelocityChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetMoverMaxVelocityText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Acceleration:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(3000.0f)
								.Value(this, &SNavGen3DWindow::GetMoverAccelerationValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnMoverAccelerationChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetMoverAccelerationText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Turn Rate:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(720.0f)
								.Value(this, &SNavGen3DWindow::GetMoverTurnRateValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnMoverTurnRateChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetMoverTurnRateText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Pathing Angle:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(180.0f)
								.Value(this, &SNavGen3DWindow::GetMoverPathingAngleValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnMoverPathingAngleChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetMoverPathingAngleText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Approach Distance:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SSlider)
								.MinValue(0.0f).MaxValue(10000.0f)
								.Value(this, &SNavGen3DWindow::GetMoverApproachDistanceValue)
								.OnValueChanged(this, &SNavGen3DWindow::OnMoverApproachDistanceChanged)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetMoverApproachDistanceText).ColorAndOpacity(TextColor) ]
						]
					]
				]

				// ---- Stats ----
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						SNew(STextBlock)
						.Text(FText::FromString("Stats"))
						.ColorAndOpacity(LabelColor)
					]
					.BodyContent()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Bounds Volumes:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetBoundsVolumeCountText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Unprocessed Volumes:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetUnprocessedVolumeCountText).ColorAndOpacity(TextColor) ]
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 4.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(FText::FromString("Solution Volumes:  ")).ColorAndOpacity(TextColor) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[ SNew(STextBlock).Text(this, &SNavGen3DWindow::GetSolutionVolumeCountText).ColorAndOpacity(TextColor) ]
						]
					]
				]
			]
		]
	];
}

void SNavGen3DWindow::AddLogEntry(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage)
{
}

float SNavGen3DWindow::GetDebugLevel() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return (float)Subsystem->DebugLevel;
	}
	return 0.0f;
}

FText SNavGen3DWindow::GetDebugLevelText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return FText::FromString(FString::Printf(TEXT("%d"), Subsystem->DebugLevel));
	}
	return FText::FromString(TEXT("0"));
}

void SNavGen3DWindow::OnDebugLevelChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugLevel = FMath::RoundToInt(InNewValue);
	}
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

FText SNavGen3DWindow::GetCameraVolumeText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		const FVector CameraLocation = Subsystem->GetCameraLocation();
		if (NavMeshVolume* Volume = Subsystem->FindVolumeContainingLocation(CameraLocation))
		{
			return FText::FromString(FString::Printf(TEXT("[%llu] %s"),
				Volume->ID,
				*UNavGen3DSubsystem::FVectorToString(Volume->Bounds.GetCenter())));
		}
	}
	return FText::FromString(TEXT("None"));
}

FText SNavGen3DWindow::GetCameraLocationText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return FText::FromString(UNavGen3DSubsystem::FVectorToString(Subsystem->GetCameraLocation()));
	}
	return FText::GetEmpty();
}

float SNavGen3DWindow::GetNavMeshAgentSliderValue() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return (float)Subsystem->DebugNavMeshAgentIndex;
	}
	return 0.0f;
}

float SNavGen3DWindow::GetNavMeshAgentSliderMax() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return (float)FMath::Max(0, Subsystem->GetSupportedAgentCount() - 1);
	}
	return 0.0f;
}

void SNavGen3DWindow::OnNavMeshAgentSliderChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugNavMeshAgentIndex = FMath::RoundToInt(InNewValue);
	}
}

FText SNavGen3DWindow::GetNavMeshAgentText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->GetSupportedAgentCount() > 0)
		{
			return FText::AsNumber(Subsystem->DebugNavMeshAgentIndex);
		}
	}
	return FText::FromString(TEXT("-"));
}

void SNavGen3DWindow::OnDebugDrawTimeChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugDrawTime = InNewValue;
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


ECheckBoxState SNavGen3DWindow::GetDrawModeRadioState(ENavGen3DDrawMode InMode) const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DebugDrawMode == InMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return InMode == ENavGen3DDrawMode::None ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawModeRadioChanged(ECheckBoxState InNewState, ENavGen3DDrawMode InMode)
{
	if (InNewState == ECheckBoxState::Checked)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->DebugDrawMode = InMode;
		}
	}
}

ECheckBoxState SNavGen3DWindow::GetDrawConnectionsState() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DebugDrawConnections ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawConnectionsChanged(ECheckBoxState InNewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugDrawConnections = (InNewState == ECheckBoxState::Checked);
	}
}

ECheckBoxState SNavGen3DWindow::GetDrawConnectivityState() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DrawConnectivity ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnDrawConnectivityChanged(ECheckBoxState InNewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DrawConnectivity = (InNewState == ECheckBoxState::Checked);
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

void SNavGen3DWindow::OnDrawCameraVolumeChanged(ECheckBoxState InNewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DrawCameraVolume = (InNewState == ECheckBoxState::Checked);
	}
}

void SNavGen3DWindow::OnVolumeIDTextChanged(const FText& InText)
{
	VolumeIDText = InText.ToString();
}

void SNavGen3DWindow::OnVolumeIDTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnEnter)
	{
		if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		{
			Subsystem->DrawVolumeID = FCString::Strtoui64(*VolumeIDText, nullptr, 10);
		}
	}
}

bool SNavGen3DWindow::HasSelectedBoundsVolume() const
{
	if (GEditor)
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (Cast<ANavGen3DBoundsVolume>(*It))
			{
				return true;
			}
		}
	}
	return false;
}

EVisibility SNavGen3DWindow::GetGenerationStatusVisibility() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->IsPlayMode() || !HasSelectedBoundsVolume())
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

FText SNavGen3DWindow::GetGenerationStatusText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->IsPlayMode())
		{
			return FText::FromString("Generation valid at Editor time");
		}
		if (!HasSelectedBoundsVolume())
		{
			return FText::FromString("Select a Bounds Volume for Generation");
		}
	}
	return FText::GetEmpty();
}

bool SNavGen3DWindow::IsNotPlayMode() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return !Subsystem->IsPlayMode();
	}
	return true;
}

bool SNavGen3DWindow::IsGenerationEnabled() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return !Subsystem->IsPlayMode() && HasSelectedBoundsVolume();
	}
	return true;
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

FReply SNavGen3DWindow::OnGenerateVolumesClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (GEditor)
		{
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (ANavGen3DBoundsVolume* Volume = Cast<ANavGen3DBoundsVolume>(*It))
				{
					Subsystem->GenerateNavMesh3DFromBoundsVolume(Volume);
				}
			}
		}
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnFindConnectionsClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->GenerateNavMesh3DConnections(Subsystem->DebugNavMeshAgentIndex);
	}
	return FReply::Handled();
}

FText SNavGen3DWindow::GetDebugPathOriginText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugPathOrigin != UNavGen3DSubsystem::InvalidLocation)
		{
			return FText::FromString(UNavGen3DSubsystem::FVectorToString(Subsystem->DebugPathOrigin));
		}
	}
	return FText::GetEmpty();
}

FText SNavGen3DWindow::GetDebugPathDestinationText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugPathDestination != UNavGen3DSubsystem::InvalidLocation)
		{
			return FText::FromString(UNavGen3DSubsystem::FVectorToString(Subsystem->DebugPathDestination));
		}
	}
	return FText::GetEmpty();
}

EVisibility SNavGen3DWindow::GetPathOriginStatusVisibility() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugLevel == 0)
		{
			return EVisibility::Visible;
		}
		return Subsystem->DebugPathOrigin == UNavGen3DSubsystem::InvalidLocation ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::Visible;
}

FText SNavGen3DWindow::GetPathOriginStatusText() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugLevel == 0)
		{
			return FText::FromString("Set the Debug Level >= 1");
		}
	}
	return FText::FromString("Set the Path Origin");
}

FReply SNavGen3DWindow::OnResetPathClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugPathSearchSpace.Reset();
		Subsystem->DebugPathOrigin = UNavGen3DSubsystem::InvalidLocation;
		Subsystem->DebugPathDestination = UNavGen3DSubsystem::InvalidLocation;
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnSetPathOriginClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		const FVector CameraLocation = Subsystem->GetCameraLocation();
		if (UWorld* World = Subsystem->FindWorld())
		{
			const float UnpaddedRadius = Subsystem->GetAgentCollisionRadius(Subsystem->DebugNavMeshAgentIndex, false);
			const FCollisionShape Sphere = FCollisionShape::MakeSphere(UnpaddedRadius);
			if (World->OverlapBlockingTestByChannel(CameraLocation, FQuat::Identity, ECC_WorldStatic, Sphere))
			{
				Subsystem->AddLogMessage(ENavGen3DLogCategory::Warning, TEXT(""), TEXT("Invalid Path Origin"));
				Subsystem->DebugPathOrigin = UNavGen3DSubsystem::InvalidLocation;
				return FReply::Handled();
			}
		}
		Subsystem->DebugPathOrigin = CameraLocation;
	}
	return FReply::Handled();
}

ECheckBoxState SNavGen3DWindow::GetPathSmoothingState() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->PathSmoothingEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SNavGen3DWindow::OnPathSmoothingChanged(ECheckBoxState InNewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->PathSmoothingEnabled = (InNewState == ECheckBoxState::Checked);
	}
}

FReply SNavGen3DWindow::OnRePathClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugRepathContinuous = false;
		Subsystem->DebugFindPath();
		Subsystem->DebugValidatePath(Subsystem->DebugPathSearchSpace);
	}
	return FReply::Handled();
}

void SNavGen3DWindow::OnDebugRepathContinuousChanged(ECheckBoxState InNewState)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugRepathContinuous = (InNewState == ECheckBoxState::Checked);
		Subsystem->DebugRepathTimer = 0.0f;
	}
}

FReply SNavGen3DWindow::OnPathToCameraClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugFindPath(UNavGen3DSubsystem::InvalidLocation, Subsystem->GetCameraLocation());
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

FReply SNavGen3DWindow::OnProcessNextVolumeClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		TOptional<NavMeshVolume> NextVolume = Subsystem->PopNextProcessingVolume();
		if (!NextVolume.IsSet())
		{
			const FVector CameraLocation = Subsystem->GetCameraLocation();
			NextVolume = Subsystem->FindGenerationVolumeContainingLocation(CameraLocation, true);
			if (!NextVolume.IsSet())
			{
				NextVolume = Subsystem->FindClosestGenerationVolume(CameraLocation, true);
			}
		}
		if (NextVolume.IsSet())
		{
			Subsystem->DebugProcessSingleVolume(NextVolume.GetValue(), true);
		}
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnProcessRemainingVolumesClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->GenerateNavMesh3D();
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnProcessCameraVolumeClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		const FVector CameraLocation = Subsystem->GetCameraLocation();

		TOptional<NavMeshVolume> GenVolume = Subsystem->FindGenerationVolumeContainingLocation(CameraLocation, true);
		if (!GenVolume.IsSet())
		{
			GenVolume = Subsystem->FindClosestGenerationVolume(CameraLocation, true);
		}
		if (GenVolume.IsSet())
		{
			Subsystem->DebugProcessSingleVolume(GenVolume.GetValue(), true);
		}
	}
	return FReply::Handled();
}

FReply SNavGen3DWindow::OnUndoLastProcessClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugUndoLastProcess();
	}
	return FReply::Handled();
}

bool SNavGen3DWindow::IsUndoLastProcessEnabled() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->HasDebugUndoState();
	}
	return false;
}

bool SNavGen3DWindow::IsPathFindingEnabled() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->DebugLevel >= 1;
	}
	return false;
}

bool SNavGen3DWindow::IsCameraLocationValidForAgent() const
{
	if (const UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		return Subsystem->bCameraLocationValidForAgent;
	}
	return false;
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

FReply SNavGen3DWindow::OnFindCameraVolumeConnectionsClicked()
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		const FVector CameraLocation = Subsystem->GetCameraLocation();
		NavMeshVolume* Volume = Subsystem->FindVolumeContainingLocation(CameraLocation);
		if (Volume)
		{
			const FCollisionShape Sphere = FCollisionShape::MakeSphere(Subsystem->GetAgentCollisionRadius(Subsystem->DebugNavMeshAgentIndex, true));
			Subsystem->FindNavMeshVolumeConnections(Subsystem->DebugNavMeshAgentIndex, Sphere, *Volume);
			if (UWorld* World = Subsystem->FindWorld())
			{
				if (const TArray<FNavVolumeConnection>* Connections = Subsystem->GetVolumeConnections(Subsystem->DebugNavMeshAgentIndex, Volume->ID))
				{
					for (const FNavVolumeConnection& Conn : *Connections)
					{
						DrawDebugSphere(World, Conn.Location, 15.0f, 8, FColor::Green, false, Subsystem->DebugDrawTime);
					}
				}
			}
		}
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

float SNavGen3DWindow::GetMoverAgentIndexValue() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return (float)Mover->AgentIndex;
	return 0.0f;
}

void SNavGen3DWindow::OnMoverAgentIndexChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		ForEachMover(Subsystem, [InNewValue](ANavGen3DMover* M) { M->AgentIndex = FMath::RoundToInt(InNewValue); });
}

FText SNavGen3DWindow::GetMoverAgentIndexText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return FText::AsNumber(Mover->AgentIndex);
	return FText::FromString(TEXT("-"));
}

float SNavGen3DWindow::GetMoverMaxVelocityValue() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugMoverMaxVelocity.IsSet())
			return Subsystem->DebugMoverMaxVelocity.GetValue();
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return Mover->SimMaxVelocity;
	}
	return 0.0f;
}

void SNavGen3DWindow::OnMoverMaxVelocityChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugMoverMaxVelocity = InNewValue;
		ForEachMover(Subsystem, [InNewValue](ANavGen3DMover* M) { M->SimMaxVelocity = InNewValue; });
	}
}

FText SNavGen3DWindow::GetMoverMaxVelocityText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugMoverMaxVelocity.IsSet())
			return FText::FromString(FString::Printf(TEXT("%.0f"), Subsystem->DebugMoverMaxVelocity.GetValue()));
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return FText::FromString(FString::Printf(TEXT("%.0f"), Mover->SimMaxVelocity));
	}
	return FText::FromString(TEXT("-"));
}

float SNavGen3DWindow::GetMoverAccelerationValue() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugMoverAcceleration.IsSet())
			return Subsystem->DebugMoverAcceleration.GetValue();
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return Mover->SimAcceleration;
	}
	return 0.0f;
}

void SNavGen3DWindow::OnMoverAccelerationChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->DebugMoverAcceleration = InNewValue;
		ForEachMover(Subsystem, [InNewValue](ANavGen3DMover* M) { M->SimAcceleration = InNewValue; });
	}
}

FText SNavGen3DWindow::GetMoverAccelerationText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		if (Subsystem->DebugMoverAcceleration.IsSet())
			return FText::FromString(FString::Printf(TEXT("%.0f"), Subsystem->DebugMoverAcceleration.GetValue()));
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return FText::FromString(FString::Printf(TEXT("%.0f"), Mover->SimAcceleration));
	}
	return FText::FromString(TEXT("-"));
}

float SNavGen3DWindow::GetMoverTurnRateValue() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return Mover->SimTurnRate;
	return 0.0f;
}

void SNavGen3DWindow::OnMoverTurnRateChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		ForEachMover(Subsystem, [InNewValue](ANavGen3DMover* M) { M->SimTurnRate = InNewValue; });
}

FText SNavGen3DWindow::GetMoverTurnRateText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return FText::FromString(FString::Printf(TEXT("%.0f"), Mover->SimTurnRate));
	return FText::FromString(TEXT("-"));
}

float SNavGen3DWindow::GetMoverPathingAngleValue() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return Mover->SimPathingAngle;
	return 0.0f;
}

void SNavGen3DWindow::OnMoverPathingAngleChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		ForEachMover(Subsystem, [InNewValue](ANavGen3DMover* M) { M->SimPathingAngle = InNewValue; });
}

FText SNavGen3DWindow::GetMoverPathingAngleText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return FText::FromString(FString::Printf(TEXT("%.0f"), Mover->SimPathingAngle));
	return FText::FromString(TEXT("-"));
}

float SNavGen3DWindow::GetMoverApproachDistanceValue() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return Mover->ApproachDistance;
	return 0.0f;
}

void SNavGen3DWindow::OnMoverApproachDistanceChanged(float InNewValue)
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		ForEachMover(Subsystem, [InNewValue](ANavGen3DMover* M) { M->ApproachDistance = InNewValue; });
}

FText SNavGen3DWindow::GetMoverApproachDistanceText() const
{
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
		if (ANavGen3DMover* Mover = GetFirstMover(Subsystem))
			return FText::FromString(FString::Printf(TEXT("%.0f"), Mover->ApproachDistance));
	return FText::FromString(TEXT("-"));
}

NavGen3D_ENABLE_OPTIMIZATION
