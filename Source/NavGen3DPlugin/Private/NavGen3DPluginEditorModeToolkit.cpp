// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DPluginEditorModeToolkit.h"
#include "NavGen3DPluginEditorMode.h"
#include "Engine/Selection.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "NavGen3DPluginEditorModeToolkit"

FNavGen3DPluginEditorModeToolkit::FNavGen3DPluginEditorModeToolkit()
{
}

void FNavGen3DPluginEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
}

void FNavGen3DPluginEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FNavGen3DPluginEditorModeToolkit::GetToolkitFName() const
{
	return FName("NavGen3DPluginEditorMode");
}

FText FNavGen3DPluginEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "NavGen3DPluginEditorMode Toolkit");
}

#undef LOCTEXT_NAMESPACE
