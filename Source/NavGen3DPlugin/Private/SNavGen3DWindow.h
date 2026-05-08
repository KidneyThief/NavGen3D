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
	void AddLogEntry(ENavGen3DLogCategory Category, const FString& ActorName, const FString& Message);

private:
	// Left panel
	ECheckBoxState GetDrawModeRadioState(ENavGen3DDrawMode Mode) const;
	void OnDrawModeRadioChanged(ECheckBoxState NewState, ENavGen3DDrawMode Mode);
	ECheckBoxState GetDrawCameraVolumeState() const;
	void OnDrawCameraVolumeChanged(ECheckBoxState NewState);
	FReply OnValidateEmbeddedBoundsVolumesClicked();
	FReply OnGenerateNavMesh3DForSelectionClicked();
	FReply OnProcessCameraVolumeClicked();
	FReply OnValidateCameraVolumeClicked();
	FReply OnResetNavMesh3DClicked();
	FReply OnValidateNavMesh3DClicked();
	float GetDebugDrawTime() const;
	FText GetDebugDrawTimeText() const;
	void OnDebugDrawTimeChanged(float NewValue);

	// Stats panel
	FText GetBoundsVolumeCountText() const;
	FText GetUnprocessedVolumeCountText() const;
	FText GetSolutionVolumeCountText() const;

	// Right panel
	FReply OnClearDebugClicked();
	FReply OnClearLogClicked();
	void OnSearchTextChanged(const FText& InText);
	void RefreshFilteredLog();
	TSharedRef<ITableRow> GenerateLogRow(TSharedPtr<FNavGen3DLogEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);

	TArray<TSharedPtr<FNavGen3DLogEntry>> LogEntries;
	TArray<TSharedPtr<FNavGen3DLogEntry>> FilteredLogEntries;
	TSharedPtr<SListView<TSharedPtr<FNavGen3DLogEntry>>> LogListView;
	FString SearchText;
};
