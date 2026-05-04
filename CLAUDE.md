# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NavGen3DPlugin is an Unreal Engine 5 **Runtime plugin** (described as "Autogenerate a 3D NavMesh" by Tim Andersen). It integrates with UE5's `InteractiveToolsFramework` via a custom `UEdMode`.

## Building

This plugin is built via **Unreal Build Tool (UBT)** — there is no standalone build script. Build from within the Unreal Editor (Ctrl+Alt+F11 for Live Coding, or build from Visual Studio) or via command line:

```
# From the engine root (adjust paths for your UE5 install)
UnrealBuildTool.exe NavGen3DPluginEditor Win64 Development -Project="<path>/NavGen3D.uproject"
```

The plugin module name for UBT is `NavGen3DPlugin`. Module type is `Runtime`, loading phase `Default`.

## Architecture

### Module Entry Point
`FNavGen3DPluginModule` (`NavGen3DPluginModule.h/.cpp`) — registers/unregisters the editor mode commands on startup/shutdown.

### Editor Mode
`UNavGen3DPluginEditorMode` (`NavGen3DPluginEditorMode.h/.cpp`) — the core `UEdMode` subclass. Registers tool builders in `Enter()` and creates the toolkit. The mode ID is `EM_NavGen3DPluginEditorMode`.

### Toolkit / UI Panel
`FNavGen3DPluginEditorModeToolkit` — creates the tool palette panel in the editor sidebar.

### Commands
`FNavGen3DPluginEditorModeCommands` — registers UI commands (`SimpleTool`, `InteractiveTool`) that appear in the mode toolbar.

### Tools (in `Private/Tools/`)
Each tool follows a three-class pattern: **Builder** → **Properties (UObject)** → **Tool**:

| Tool | Class | Base | Trigger |
|---|---|---|---|
| Actor Info | `UNavGen3DPluginSimpleTool` | `USingleClickTool` | Left-click |
| Measure Distance | `UNavGen3DPluginInteractiveTool` | `UInteractiveTool` + `IClickDragBehaviorTarget` | Click-drag; Shift to set second point |

### Dependencies (`Build.cs`)
Private: `CoreUObject`, `Engine`, `Slate`, `SlateCore`, `InputCore`, `EditorFramework`, `EditorStyle`, `UnrealEd`, `LevelEditor`, `InteractiveToolsFramework`, `EditorInteractiveToolsFramework`

## Adding a New Tool

The codebase has inline step markers guiding this process:

1. **Step 1** (`NavGen3DPluginEditorMode.cpp` top): `#include` your new tool header.
2. **Step 2** (`UNavGen3DPluginEditorMode::Enter()`): Call `RegisterTool(command, toolName, NewObject<YourToolBuilder>(this))`.
3. Add a `UI_COMMAND` entry in `FNavGen3DPluginEditorModeCommands::RegisterCommands()`.
4. Expose the command via `GetModeCommands()` (already handled generically).

New tool headers go in `Private/Tools/` and must use `NAVGEN3DPLUGIN_API` for exported classes.

## UHT / Generated Code

UHT output lives in `Intermediate/Build/Win64/UnrealEditor/Inc/NavGen3DPlugin/UHT/`. Any class with `UCLASS()`, `UPROPERTY()`, or `UFUNCTION()` macros requires a `.generated.h` include and triggers UHT regeneration on build.
