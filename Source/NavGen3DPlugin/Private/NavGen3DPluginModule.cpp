// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavGen3DPluginModule.h"
#include "NavGen3DPluginEditorModeCommands.h"
#include "NavGen3DSubsystem.h"
#include "SNavGen3DWindow.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "NavGen3DSettings.h"

// -- defined in NavGen3DSettings.h
NavGen3D_DISABLE_OPTIMIZATION

#define LOCTEXT_NAMESPACE "NavGen3DPluginModule"

const FName FNavGen3DPluginModule::NavGen3DTabName = "NavGen3DWindow";

void FNavGen3DPluginModule::StartupModule()
{
	FNavGen3DPluginEditorModeCommands::Register();

	NavGen3DMenuGroup = WorkspaceMenu::GetMenuStructure().GetStructureRoot()->AddGroup(
		LOCTEXT("NavGen3DMenuGroup", "NavGen3D")
	);

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		NavGen3DTabName,
		FOnSpawnTab::CreateRaw(this, &FNavGen3DPluginModule::SpawnNavGen3DTab)
	)
	.SetDisplayName(LOCTEXT("NavGen3DTabTitle", "NavGen3D"))
	.SetMenuType(ETabSpawnerMenuType::Enabled)
	.SetGroup(NavGen3DMenuGroup.ToSharedRef());
}

void FNavGen3DPluginModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(NavGen3DTabName);
	NavGen3DMenuGroup.Reset();
	FNavGen3DPluginEditorModeCommands::Unregister();
}

TSharedRef<SDockTab> FNavGen3DPluginModule::SpawnNavGen3DTab(const FSpawnTabArgs& InSpawnTabArgs)
{
	TSharedRef<SNavGen3DWindow> Window = SNew(SNavGen3DWindow);
	if (UNavGen3DSubsystem* Subsystem = GEngine->GetEngineSubsystem<UNavGen3DSubsystem>())
	{
		Subsystem->SetNavGen3DWindow(Window);
	}
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[Window];

	Tab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateLambda(
		[](TSharedRef<SDockTab> InTab, ETabActivationCause)
		{
			if (TSharedPtr<SWindow> TabWindow = InTab->GetParentWindow())
			{
				TabWindow->SetSizeLimits(FWindowSizeLimits()
					.SetMinWidth(620.0f)
					.SetMinHeight(520.0f));
			}
		}
	));

	return Tab;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNavGen3DPluginModule, NavGen3DPluginEditorMode)

NavGen3D_ENABLE_OPTIMIZATION