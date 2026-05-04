// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DPluginEditorModeCommands.h"
#include "NavGen3DPluginEditorMode.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "NavGen3DPluginEditorModeCommands"

FNavGen3DPluginEditorModeCommands::FNavGen3DPluginEditorModeCommands()
	: TCommands<FNavGen3DPluginEditorModeCommands>("NavGen3DPluginEditorMode",
		NSLOCTEXT("NavGen3DPluginEditorMode", "NavGen3DPluginEditorModeCommands", "NavGen3DPlugin Editor Mode"),
		NAME_None,
		FEditorStyle::GetStyleSetName())
{
}

void FNavGen3DPluginEditorModeCommands::RegisterCommands()
{
	TArray <TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);

	UI_COMMAND(SimpleTool, "Show Actor Info", "Opens message box with info about a clicked actor", EUserInterfaceActionType::Button, FInputChord());
	ToolCommands.Add(SimpleTool);

	UI_COMMAND(InteractiveTool, "Measure Distance", "Measures distance between 2 points (click to set origin, shift-click to set end point)", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(InteractiveTool);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FNavGen3DPluginEditorModeCommands::GetCommands()
{
	return FNavGen3DPluginEditorModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
