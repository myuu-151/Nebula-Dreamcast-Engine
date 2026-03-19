# Editor Play Mode

This document covers the editor's play mode system: script compilation, DLL caching, progress feedback, MSVC detection, hot reloading, and scene switching behavior.

---

## Overview

When you press **Play** in the editor, it:
1. Checks MSVC availability
2. Snapshots all open scenes (for fresh reloads on scene switch)
3. Switches to the project's default scene (matching Dreamcast boot behavior)
4. Compiles gameplay scripts to DLLs in the background
5. Loads DLLs and calls `NB_Game_OnStart` on all scripts
6. Runs `NB_Game_OnUpdate(dt)` each frame until you press **Stop**

---

## Script Compilation

### Async compilation

Script compilation runs on a background thread so the editor UI stays responsive. The main thread polls for completion each frame via `PollPlayScriptCompile()`.

**State machine** (`gScriptCompileState`):
- **0 = idle** — no compilation in progress
- **1 = compiling** — background thread is running MSVC
- **2 = done** — thread finished, main thread loads DLLs

While compiling:
- A **progress bar overlay** renders in the viewport center showing "Compiling scripts... X / Y"
- **Physics is deferred** — gravity, ground snap, and slope alignment do not run
- **Scripts do not tick** — `OnUpdate` is not called until compilation finishes

### DLL output

Each unique script compiles to its own DLL in `<project>/Intermediate/EditorScript/`:
- `nb_script_0.dll`, `nb_script_1.dll`, etc.
- Each DLL exports `NB_Game_OnStart`, `NB_Game_OnUpdate`, `NB_Game_OnSceneSwitch`
- A bridge source file is generated alongside each DLL containing `NB_RT_*` stub functions that route calls back into the editor

### MSVC detection

Before compiling, the editor checks for `cl.exe`:
1. Runs `where cl` to check PATH
2. Falls back to the user's configured vcvars path in preferences
3. Auto-discovers VS2022 BuildTools if no explicit path is set

If MSVC is not found:
- A toast appears: **"MSVC not found! Set PATH in File > Preferences"**
- Play mode is **fully reverted** — no gravity, no physics, no scripts run
- The editor returns to edit mode immediately

---

## DLL Caching

The editor skips recompilation when a script hasn't changed. Before compiling each script, it compares the DLL's last-modified time against the source `.c` file's last-modified time. If the DLL is newer than the source, compilation is skipped entirely.

This makes repeated Play presses near-instant when scripts haven't been edited.

The cache check runs **before** the bridge file is written to avoid the bridge file's timestamp invalidating the cache.

---

## Progress Bar

During compilation, a centered overlay appears in the viewport:

```
[ Compiling scripts... 2 / 3        ]
[==================                  ]
```

- Dark semi-transparent background
- Fill bar shows completion percentage
- Text shows scripts compiled so far vs total
- Disappears automatically when compilation finishes

---

## Scene Switching in Play Mode

### Scene snapshots

When play mode starts, the editor takes a snapshot of all open scenes into `gPlayOriginalScenes`. This stores the original node arrays (positions, rotations, physics state) before any runtime modifications.

When a scene switch occurs during play (via `NB_RT_SwitchScene`, `NextScene`, or `PrevScene`), the editor reloads the target scene from `gPlayOriginalScenes` — not from the current (potentially modified) scene data. This matches Dreamcast behavior where scenes are loaded fresh from disc.

**Why this matters:** Without snapshots, if the player moves to a trigger zone and switches scenes, then switches back, the player would spawn at the trigger zone position (where they were when they left) instead of the scene's original spawn point. This caused re-trigger loops before the fix.

### Default scene boot

On play-mode start, the editor switches to the project's configured default scene. This matches Dreamcast runtime behavior where the first scene loaded is always the default.

### Tab synchronization

Scene switches during play mode use `gForceSelectSceneTab` to force the ImGui tab UI to select the correct scene tab. Without this, the tab UI would override the active scene index on the next frame, causing the switch to appear to fail.

If the target scene isn't already open as a tab, the editor auto-loads it from disc (`.nebscene` file) and adds it as a new tab.

### Play-mode stop

When play mode stops:
- All scenes revert to their pre-play state
- Camera reverts to its pre-play position and orientation
- Scene snapshots are cleared
- Compilation thread is joined (if still running)

---

## Hot Reload

The editor can watch for script file changes and reload DLLs without stopping play mode.

### Enabling

**File > Enable Hot Reloading** toggles the watcher on/off. When enabled, the editor polls the project's `Scripts/` folder every **0.75 seconds** for file modification time changes.

### What triggers a reload

- A `.c` file in `Scripts/` is saved (modification time changed)
- A new `.c` file is added
- A `.c` file is removed

### Feedback

When changes are detected:
- **Single file**: toast shows `"Script updated: <filename>.c"`
- **Multiple files**: toast shows `"Scripts updated: N file(s)"`
- A generation counter increments for tracking

### Manual reload

**File > Reload Scripts Now** forces an immediate reload regardless of file modification times.

---

## Editor Preferences

Play mode depends on two preference fields stored in `editor_prefs.ini` (next to the editor executable):

- **DreamSDK Home** — path to DreamSDK root (used for Dreamcast packaging, not play mode)
- **MSVC Path** — path to `vcvarsall.bat` (used to set up the MSVC compilation environment)

Both are configured in **File > Preferences**. The dialog shows green/red status indicators for path validation.

Preferences persist across editor restarts. The INI file uses an absolute path resolved from the editor executable's location, so it works regardless of the working directory.

---

## Troubleshooting

### "MSVC not found!" on Play
- Open **File > Preferences** and set the MSVC path to your `vcvarsall.bat`
- Or launch the editor from a Visual Studio Developer Command Prompt so `cl.exe` is in PATH

### Play mode applies gravity but nothing moves
- Scripts are still compiling — wait for the progress bar to finish
- If no progress bar appears and MSVC error was shown, fix the MSVC path first

### Scene switch works once but not back
- Ensure both scenes are saved as `.nebscene` files in the project
- The editor auto-loads scenes from disc during play-mode switches, but the file must exist

### Hot reload not detecting changes
- Check that **File > Enable Hot Reloading** is checked
- Ensure scripts are in the project's `Scripts/` folder (not a subfolder)
- The poll interval is 0.75s — wait a moment after saving
