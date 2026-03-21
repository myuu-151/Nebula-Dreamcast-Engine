# VMU Tool

The VMU Tool is Nebula's built-in editor for creating and animating Sega Dreamcast VMU (Visual Memory Unit) LCD icons. The VMU's LCD is a 48x32 monochrome (1-bit) display — each pixel is either on or off.

## Overview

The tool lets you:

- Import 48x32 PNG images and convert them to monochrome bitmaps
- Draw/erase pixels directly on a grid with undo support
- Build frame-by-frame animations using a layer/timeline system
- Link animations to the project so they persist across sessions
- Preview playback at configurable speeds with loop control
- Export icons and animations for Dreamcast runtime (load-on-boot, animated LCD)

When the VMU Tool is open, it takes over the entire editor viewport — the 3D scene is not rendered.

## Source Files

| File | Purpose |
|------|---------|
| `src/vmu/vmu_tool.h` | `VmuAnimLayer` struct, all `gVmu*` state globals, load/save function declarations |
| `src/vmu/vmu_tool.cpp` | VMU state definitions, PNG-to-mono conversion, `.vmuanim` frame data serialization |
| `src/vmu/vmu_tool_ui.cpp` | Full ImGui UI: grid preview, asset browser, timeline, layer panel, draw mode |
| `src/io/texture_io.cpp` | `SaveVmuMonoPng` — WIC-based PNG export of the 48x32 mono bitmap |
| `src/editor/file_dialogs.cpp` | `PickPngFileDialog`, `PickVmuFrameDataDialog` — Win32 open file dialogs |
| `src/editor/project.cpp` | `Get/SetProjectVmuLoadOnBoot`, `Get/SetProjectVmuAnim` — Config.ini persistence |

## Data Model

### Monochrome Bitmap

The core data is `gVmuMono`, a flat `std::array<uint8_t, 48 * 32>` (1536 bytes). Each element is `0` (off/white) or `1` (on/black). Layout is row-major, top-left origin: pixel at `(x, y)` is `gVmuMono[y * 48 + x]`.

### VmuAnimLayer

```c
struct VmuAnimLayer {
    std::string name;          // display name (e.g. "Layer 1")
    bool        visible;       // whether this layer contributes to playback
    int         frameStart;    // first frame this layer is active (inclusive)
    int         frameEnd;      // last frame this layer is active (inclusive)
    std::string linkedAsset;   // path to linked PNG file (absolute)
};
```

Each layer maps a PNG image to a frame range on the timeline. During playback, the tool scans layers in order and displays the last visible layer whose range contains the current playhead position.

### State Globals

| Global | Type | Default | Purpose |
|--------|------|---------|---------|
| `gShowVmuTool` | `bool` | `false` | Whether the VMU Tool is open (controls viewport takeover) |
| `gVmuHasImage` | `bool` | `false` | Whether `gVmuMono` contains valid pixel data |
| `gVmuLoadOnBoot` | `bool` | `false` | Whether to bake the icon into the Dreamcast build for load-on-boot |
| `gVmuAssetPath` | `string` | `""` | Path of the currently loaded/saved PNG |
| `gVmuMono` | `array<uint8_t, 1536>` | all zeros | The 48x32 monochrome bitmap |
| `gVmuAnimLayers` | `vector<VmuAnimLayer>` | 1 default layer | Animation layer stack |
| `gVmuAnimLayerSel` | `int` | `0` | Currently selected layer index |
| `gVmuAnimTotalFrames` | `int` | `24` | Total frame count on the timeline |
| `gVmuAnimPlayhead` | `int` | `0` | Current playback position (0-based) |
| `gVmuAnimLoop` | `bool` | `false` | Whether playback loops |
| `gVmuAnimSpeedMode` | `int` | `1` | Speed: 0 = x0.5 (4 fps), 1 = x1 (8 fps), 2 = x2 (16 fps) |
| `gVmuCurrentLoadedType` | `int` | `0` | What was last loaded: 0 = nothing, 1 = PNG, 2 = VMUAnim |
| `gVmuLinkedPngPath` | `string` | `""` | Path to the linked source PNG |
| `gVmuLinkedAnimPath` | `string` | `""` | Path to the linked `.vmuanim` file (persisted to project) |

## PNG Import and Conversion

`LoadVmuPngToMono(path, outErr)` loads a PNG via WIC (`LoadImageWIC`) and converts it to monochrome:

1. Image must be exactly 48x32 pixels. Any other size is rejected.
2. For each pixel, compute luminance: `lum = (30*R + 59*G + 11*B) / 100`
3. Pixel is set to 1 (on) if `alpha > 127` AND `lum < 128`. Otherwise 0 (off).
4. Result is written directly into `gVmuMono`.

This means: dark, opaque pixels become "on" (black on the VMU LCD). Transparent or bright pixels become "off" (clear).

## PNG Export

`SaveVmuMonoPng(outPath, mono)` writes the monochrome bitmap back to a PNG file using WIC. Each mono pixel maps to either fully black (`#000000FF`) or fully white (`#FFFFFFFF`). Saved to `Assets/VMU/` in the project directory with auto-incrementing filenames (`px0001.png`, `px0002.png`, ...).

## Animation Frame Data (`.vmuanim`)

The `.vmuanim` file format is a simple text format that stores the timeline and layer configuration.

### File Format

```
VMUANIM 1
TOTAL_FRAMES	24
PLAYHEAD	0
LOOP	0
SPEED_MODE	1
LAYER_COUNT	3
LAYER	Layer 1	1	0	7	C:/project/Assets/VMU/frame1.png
LAYER	Layer 2	1	8	15	C:/project/Assets/VMU/frame2.png
LAYER	Layer 3	1	16	23	C:/project/Assets/VMU/frame3.png
```

| Field | Format | Description |
|-------|--------|-------------|
| `VMUANIM` | header + version | Must be present; version is currently `1` |
| `TOTAL_FRAMES` | `\t` + integer | Total frames on the timeline (minimum 1) |
| `PLAYHEAD` | `\t` + integer | Last playhead position |
| `LOOP` | `\t` + 0/1 | Loop toggle |
| `SPEED_MODE` | `\t` + 0/1/2 | Speed: 0 = x0.5, 1 = x1, 2 = x2 |
| `LAYER_COUNT` | `\t` + integer | Number of `LAYER` lines that follow |
| `LAYER` | tab-separated fields | `name`, `visible` (0/1), `frameStart`, `frameEnd`, `linkedAsset` |

Layer names have tabs/newlines/carriage returns replaced with spaces on save. All fields are tab-delimited.

### Load Behavior

`LoadVmuFrameData(path)`:
- Requires the `VMUANIM` header line or returns `false`
- If no layers are present, creates a single default layer
- Clamps `TOTAL_FRAMES` to minimum 1
- Clamps `PLAYHEAD` to valid range
- Clamps `SPEED_MODE` to 0–2
- Sets `gVmuCurrentLoadedType = 2` on success

### Save Behavior

`SaveVmuFrameData(path)`:
- Writes all fields in the format above
- Saved to `Assets/VMU/` with auto-incrementing filenames (`framedata0001.vmuanim`, `framedata0002.vmuanim`, ...)

## Editor UI

The VMU Tool UI (`DrawVmuToolUI`) is a full-window ImGui panel with three main areas.

### Top Toolbar

| Button | Action |
|--------|--------|
| **Save** | Export current `gVmuMono` as a PNG to `Assets/VMU/` |
| **Load on boot** | Toggle `gVmuLoadOnBoot` — persisted to `Config.ini` via `SetProjectVmuLoadOnBoot`. When ON, the icon is baked into the Dreamcast build |
| **Load Asset Linked** | Reload from linked VMUAnim or PNG. Priority: linked VMUAnim (if type=2) > selected layer's linked PNG > linked VMUAnim (fallback) > current asset path |
| **Draw** | Toggle pixel draw mode. Creates a blank canvas if no image is loaded |
| **Save FrameData** | Save the current timeline/layers as a `.vmuanim` file |
| **Load FrameData** | Open file dialog to load a `.vmuanim` file |
| **Close** | Reset all layers, clear the bitmap, return to blank state |
| **X** | Close the VMU Tool (return to 3D viewport) |

### Left Panel

Split into two halves:

**Upper: Asset Browser**
- Lists all `.png` and `.vmuanim` files in `Assets/VMU/`
- Click a PNG to load it into the preview
- Click a `.vmuanim` to load its timeline data and link it to the project
- "Image..." button opens a file dialog to import an external PNG (copies to `Assets/VMU/`)
- PNG files can be dragged onto timeline layers to link them
- `.vmuanim` files can be dragged onto the "Load Asset Linked" button
- Right-click context menu: Delete

**Lower: Timeline/Layer Panel**
- Frame count input and playhead slider
- Layer add/remove buttons
- Per-layer properties: name, visible toggle, frame start/end
- Play/Stop toggle (also Space key), Loop toggle, Speed cycle (x0.5 / x1 / x2)
- Visual timeline with frame tick marks, layer bars (green), and a red playhead line
- Click a layer row to select it and load its linked PNG into preview
- Drag a PNG from the asset browser onto a layer row to link it
- Arrow button (`->`) links the currently loaded asset to the selected layer

### Right Panel: Preview Grid

- Renders the 48x32 monochrome bitmap as a scalable pixel grid
- White background, black filled cells, gray gridlines
- In draw mode: left-click draws (sets pixels to 1), right-click erases (sets to 0)
- Drawing uses Bresenham line interpolation between mouse samples for smooth strokes
- Undo stack (up to 64 entries) — Ctrl+Z to undo strokes

### Playback

During playback, the tool advances the playhead at the configured speed:

| Speed Mode | Base FPS | Multiplier |
|------------|----------|------------|
| 0 | 4 fps | x0.5 |
| 1 | 8 fps | x1 |
| 2 | 16 fps | x2 |

Frame advance accumulates `ImGui::DeltaTime` and steps when the threshold is reached. At each frame, the tool scans all visible layers in order — the last layer whose frame range contains the current playhead provides the displayed PNG. If no layer covers the current frame (in VMUAnim mode), the preview is cleared.

## Project Persistence

VMU settings are stored in the project's `Config.ini`:

```ini
vmuLoadOnBoot=1
vmuLinkedAnim=Assets/VMU/framedata0001.vmuanim
```

| Key | Stored by | Read by |
|-----|-----------|---------|
| `vmuLoadOnBoot` | `SetProjectVmuLoadOnBoot` | `GetProjectVmuLoadOnBoot` — read on project open |
| `vmuLinkedAnim` | `SetProjectVmuAnim` | `GetProjectVmuAnim` — read on project open, auto-loads the linked `.vmuanim` |

On project open (in `main_menu.cpp`), the editor reads both values and auto-loads the linked VMUAnim if it exists, opening the VMU Tool automatically.

## Dreamcast Export

When packaging for Dreamcast (`GenerateDreamcastPackage` in `dc_codegen.cpp`), VMU data is baked into the generated runtime in two forms:

### Boot Icon

If `gVmuLoadOnBoot` is enabled and there is valid bitmap data:

1. The source bitmap is resolved from `gVmuLinkedPngPath` (PNG link) or the active layer at the current playhead (VMUAnim link)
2. The 48x32 mono bitmap is packed into 192 bytes (48x32 / 8 = 192), with coordinate inversion:
   - Y is flipped: `dstY = 31 - srcY` (VMU LCD memory is vertically inverted vs editor preview)
   - X is flipped: `dstX = 47 - srcX` (horizontal orientation fix)
   - Bit packing: `byte[dstY * 6 + (dstX >> 3)] |= (1 << (7 - (dstX & 7)))`
3. The packed bytes are emitted as `kVmuBootPng[192]` in the generated `main.c`
4. Also written to `cd_root/data/vmu/vmu_boot.bin` on disc

### Animation Frames

If `gVmuLoadOnBoot` is enabled and any layer has a linked PNG:

1. For each frame in `gVmuAnimTotalFrames`, scan layers to find the active PNG
2. Load and convert each PNG to mono, pack with the same coordinate inversion
3. All frames are concatenated as `kVmuAnimFrames[]` in `main.c` (192 bytes per frame)
4. Also written to `cd_root/data/vmu/vmu_anim.bin` on disc

### Runtime Behavior

The generated Dreamcast runtime includes two VMU functions:

**`NB_TryLoadVmuBootImage()`**
- Called once at startup and once per frame
- Detects VMU LCD devices via Maple enumeration
- On first detection (or device change), draws the boot icon to the LCD via `vmu_draw_lcd`
- Prefers disc-based `vmu_boot.bin` over the baked constant (allows post-build patching)
- If animation is enabled, uses the first animation frame instead

**`NB_UpdateVmuAnim(dt)`**
- Called every frame with delta time
- Advances through animation frames at the configured speed
- Prefers disc-based `vmu_anim.bin` over baked constants
- Handles loop/one-shot based on `kVmuAnimLoop`
- Speed mapping: code 0 = 4 fps, code 1 = 8 fps, code 2 = 16 fps

### Disc Layout

```
cd_root/data/vmu/
  vmu_boot.bin       192 bytes — packed boot icon
  vmu_anim.bin       192 * N bytes — packed animation frames (N = frame count)
```

### Generated Constants

| Constant | Type | Description |
|----------|------|-------------|
| `kVmuLoadOnBoot` | `int` | 1 if boot icon is enabled |
| `kVmuBootPng[192]` | `uint8_t[]` | Packed boot icon bytes |
| `kVmuAnimEnabled` | `int` | 1 if animation has linked layers |
| `kVmuAnimLoop` | `int` | 1 if animation loops |
| `kVmuAnimSpeedCode` | `int` | 0/1/2 speed mode |
| `kVmuAnimFrameCount` | `int` | Number of animation frames |
| `kVmuAnimFrames[]` | `uint8_t[]` | All packed animation frames (192 bytes each) |

## Drag-and-Drop Interactions

| Source | Target | Action |
|--------|--------|--------|
| PNG in asset browser | Layer row in timeline | Link the PNG to that layer |
| `.vmuanim` in asset browser | "Load Asset Linked" button | Load and link the VMUAnim to the project |
| Right-click | "Load Asset Linked" button | Unlink the current VMUAnim |

## Keyboard Shortcuts

| Key | Context | Action |
|-----|---------|--------|
| Space | When text input is not focused | Toggle Play/Stop |
| Ctrl+Z | Anytime in VMU Tool | Undo last draw stroke (up to 64 levels) |
