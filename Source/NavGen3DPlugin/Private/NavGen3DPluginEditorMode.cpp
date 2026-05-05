// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DPluginEditorMode.h"
#include "NavGen3DPluginEditorModeToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "NavGen3DPluginEditorModeCommands.h"


//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
// AddYourTool Step 1 - include the header file for your Tools here
//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
#include "Tools/NavGen3DPluginInteractiveTool.h"

// step 2: register a ToolBuilder in FNavGen3DPluginEditorMode::Enter() below


#define LOCTEXT_NAMESPACE "NavGen3DPluginEditorMode"

const FEditorModeID UNavGen3DPluginEditorMode::EM_NavGen3DPluginEditorModeId = TEXT("EM_NavGen3DPluginEditorMode");

FString UNavGen3DPluginEditorMode::InteractiveToolName = TEXT("NavGen3DPlugin_MeasureDistanceTool");


UNavGen3DPluginEditorMode::UNavGen3DPluginEditorMode()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	// appearance and icon in the editing mode ribbon can be customized here
	Info = FEditorModeInfo(UNavGen3DPluginEditorMode::EM_NavGen3DPluginEditorModeId,
		LOCTEXT("ModeName", "NavGen3DPlugin"),
		FSlateIcon(),
		true);
}


UNavGen3DPluginEditorMode::~UNavGen3DPluginEditorMode()
{
}


void UNavGen3DPluginEditorMode::ActorSelectionChangeNotify()
{
}

void UNavGen3DPluginEditorMode::Enter()
{
	UEdMode::Enter();

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// AddYourTool Step 2 - register the ToolBuilders for your Tools here.
	// The string name you pass to the ToolManager is used to select/activate your ToolBuilder later.
	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 
	const FNavGen3DPluginEditorModeCommands& SampleToolCommands = FNavGen3DPluginEditorModeCommands::Get();

	RegisterTool(SampleToolCommands.InteractiveTool, InteractiveToolName, NewObject<UNavGen3DPluginInteractiveToolBuilder>(this));
}

void UNavGen3DPluginEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FNavGen3DPluginEditorModeToolkit);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UNavGen3DPluginEditorMode::GetModeCommands() const
{
	return FNavGen3DPluginEditorModeCommands::Get().GetCommands();
}

#undef LOCTEXT_NAMESPACE
