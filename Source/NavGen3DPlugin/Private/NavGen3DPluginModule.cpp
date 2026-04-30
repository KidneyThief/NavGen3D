// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DPluginModule.h"
#include "NavGen3DPluginEditorModeCommands.h"

#define LOCTEXT_NAMESPACE "NavGen3DPluginModule"

void FNavGen3DPluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FNavGen3DPluginEditorModeCommands::Register();
}

void FNavGen3DPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	FNavGen3DPluginEditorModeCommands::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNavGen3DPluginModule, NavGen3DPluginEditorMode)