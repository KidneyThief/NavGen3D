// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavGen3DLog.h"
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
	ECheckBoxState GetDrawNavBounds3DState() const;
	void OnDrawNavBounds3DChanged(ECheckBoxState NewState);
	FReply OnValidateEmbeddedBoundsVolumesClicked();
	FReply OnGenerateNavMesh3DForSelectionClicked();

	// Right panel
	void OnSearchTextChanged(const FText& InText);
	void RefreshFilteredLog();
	TSharedRef<ITableRow> GenerateLogRow(TSharedPtr<FNavGen3DLogEntry> Item, const TSharedRef<STableViewBase>& OwnerTable);

	TArray<TSharedPtr<FNavGen3DLogEntry>> LogEntries;
	TArray<TSharedPtr<FNavGen3DLogEntry>> FilteredLogEntries;
	TSharedPtr<SListView<TSharedPtr<FNavGen3DLogEntry>>> LogListView;
	FString SearchText;
};
