// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavGen3DLog.h"
#include "NavGen3DSubsystem.h"
#include "Widgets/SCompoundWidget.h"

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
	ECheckBoxState GetDrawConnectivityState() const;
	void OnDrawConnectivityChanged(ECheckBoxState InNewState);
	ECheckBoxState GetDrawCameraVolumeState() const;
	void OnDrawCameraVolumeChanged(ECheckBoxState InNewState);
	void OnVolumeIDTextChanged(const FText& InText);
	void OnVolumeIDTextCommitted(const FText& InText, ETextCommit::Type InCommitType);
	EVisibility GetPathOriginStatusVisibility() const;
	FText GetPathOriginStatusText() const;
	FText GetDebugPathOriginText() const;
	FText GetDebugPathDestinationText() const;
	FReply OnResetPathClicked();
	FReply OnSetPathOriginClicked();
	FReply OnPathToCameraClicked();
	void OnDebugRepathContinuousChanged(ECheckBoxState InNewState);
	ECheckBoxState GetPathSmoothingState() const;
	void OnPathSmoothingChanged(ECheckBoxState InNewState);
	FReply OnRePathClicked();
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
	FReply OnProcessNextVolumeClicked();
	FReply OnProcessRemainingVolumesClicked();
	FReply OnProcessCameraVolumeClicked();
	FReply OnUndoLastProcessClicked();
	bool IsUndoLastProcessEnabled() const;
	bool IsPathFindingEnabled() const;
	bool IsCameraLocationValidForAgent() const;
	FReply OnValidateCameraVolumeClicked();
	FReply OnFindCameraVolumeConnectionsClicked();
	FReply OnResetNavMesh3DClicked();
	FReply OnValidateNavMesh3DClicked();
	float GetDebugLevel() const;
	FText GetDebugLevelText() const;
	void OnDebugLevelChanged(float InNewValue);
	float GetDebugDrawTime() const;
	FText GetDebugDrawTimeText() const;
	void OnDebugDrawTimeChanged(float InNewValue);

	// Mover 3D Settings panel
	float GetMoverAgentIndexValue() const;
	void OnMoverAgentIndexChanged(float InNewValue);
	FText GetMoverAgentIndexText() const;
	float GetMoverMaxVelocityValue() const;
	void OnMoverMaxVelocityChanged(float InNewValue);
	FText GetMoverMaxVelocityText() const;
	float GetMoverAccelerationValue() const;
	void OnMoverAccelerationChanged(float InNewValue);
	FText GetMoverAccelerationText() const;
	float GetMoverTurnRateValue() const;
	void OnMoverTurnRateChanged(float InNewValue);
	FText GetMoverTurnRateText() const;
	float GetMoverPathingAngleValue() const;
	void OnMoverPathingAngleChanged(float InNewValue);
	FText GetMoverPathingAngleText() const;
	float GetMoverApproachDistanceValue() const;
	void OnMoverApproachDistanceChanged(float InNewValue);
	FText GetMoverApproachDistanceText() const;

	// Stats panel
	FText GetBoundsVolumeCountText() const;
	FText GetUnprocessedVolumeCountText() const;
	FText GetSolutionVolumeCountText() const;

	FReply OnClearDebugClicked();

	FString VolumeIDText;
};
