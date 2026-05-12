// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavGen3DLog.h"
#include "NavGen3DSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SNavGen3DWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNavGen3DWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void AddLogEntry(ENavGen3DLogCategory InCategory, const FString& InActorName, const FString& InMessage);

private:
	// Left panel
	FText GetCameraLocationText() const;
	FText GetCameraVolumeText() const;
	ECheckBoxState GetDrawModeRadioState(ENavGen3DDrawMode InMode) const;
	void OnDrawModeRadioChanged(ECheckBoxState InNewState, ENavGen3DDrawMode InMode);
	ECheckBoxState GetDrawConnectionsState() const;
	void OnDrawConnectionsChanged(ECheckBoxState InNewState);
	ECheckBoxState GetDrawConnectedState() const;
	void OnDrawConnectedChanged(ECheckBoxState InNewState);
	ECheckBoxState GetDrawCameraVolumeState() const;
	void OnDrawCameraVolumeChanged(ECheckBoxState InNewState);
	void OnVolumeIDTextChanged(const FText& InText);
	void OnVolumeIDTextCommitted(const FText& InText, ETextCommit::Type InCommitType);
	float GetNavMeshAgentSliderValue() const;
	float GetNavMeshAgentSliderMax() const;
	void OnNavMeshAgentSliderChanged(float InNewValue);
	FText GetNavMeshAgentText() const;
	bool HasSelectedBoundsVolume() const;
	EVisibility GetGenerationStatusVisibility() const;
	FText GetGenerationStatusText() const;
	bool IsNotPlayMode() const;
	bool IsGenerationEnabled() const;
	FReply OnGenerateNavMesh3DForSelectionClicked();
	FReply OnGenerateVolumesClicked();
	FReply OnFindConnectionsClicked();
	FReply OnProcessCameraVolumeClicked();
	FReply OnValidateCameraVolumeClicked();
	FReply OnFindCameraVolumeConnectionsClicked();
	FReply OnResetNavMesh3DClicked();
	FReply OnValidateNavMesh3DClicked();
	float GetDebugDrawTime() const;
	FText GetDebugDrawTimeText() const;
	void OnDebugDrawTimeChanged(float InNewValue);

	// Stats panel
	FText GetBoundsVolumeCountText() const;
	FText GetUnprocessedVolumeCountText() const;
	FText GetSolutionVolumeCountText() const;

	// Right panel
	FReply OnToggleLogPanelClicked();
	EVisibility GetLogPanelVisibility() const;
	FText GetToggleLogButtonText() const;
	FReply OnClearDebugClicked();
	FReply OnClearLogClicked();
	void OnSearchTextChanged(const FText& InText);
	void RefreshFilteredLog();
	TSharedRef<ITableRow> GenerateLogRow(TSharedPtr<FNavGen3DLogEntry> InItem, const TSharedRef<STableViewBase>& InOwnerTable);

	bool bLogPanelVisible = true;
	FString VolumeIDText;
	float CachedExpandedWindowWidth = 620.0f;
	TArray<TSharedPtr<FNavGen3DLogEntry>> LogEntries;
	TArray<TSharedPtr<FNavGen3DLogEntry>> FilteredLogEntries;
	TSharedPtr<SListView<TSharedPtr<FNavGen3DLogEntry>>> LogListView;
	FString SearchText;
};
