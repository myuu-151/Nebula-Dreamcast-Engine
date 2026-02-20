# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Core editor and engine integration code (`main.cpp`, `audio3d.*`, `nebula_dreamcast.*`).
- `assets/`: Runtime/editor assets (textures, meshes, icons). Keep generated artifacts and source assets clearly named.
- `thirdparty/`: Vendored dependencies (`glfw`, `imgui`). Treat as external code; avoid local edits unless updating vendor code intentionally.
- `build/`: Out-of-source CMake build tree. Regenerate instead of hand-editing files here.
- Root build config lives in `CMakeLists.txt` and `nebula.rc`.

## Build, Test, and Development Commands
- Configure (desktop): `cmake -S . -B build -DNEBULA_USE_DREAMCAST_SDK=OFF`
- Build: `cmake --build build --config Debug`
- Run editor (Windows): `./build/Debug/NebulaEditor.exe`
- Dreamcast SDK wiring (optional):
  - `cmake -S . -B build -DNEBULA_USE_DREAMCAST_SDK=ON -DKOS_BASE="C:/DreamSDK/opt/toolchains/dc/kos" -DKOS_CC_BASE="C:/DreamSDK/opt/toolchains/dc"`
- There is no dedicated unit-test target yet; validate changes by building and exercising edited flows in the editor.

## Coding Style & Naming Conventions
- Language: C++17 (set in `CMakeLists.txt`).
- Indentation: 4 spaces, braces on next line for functions/types, consistent with existing `src/*.cpp`.
- Naming follows current code: `PascalCase` for types/functions, `camelCase` for locals/fields, `kPrefix` for constants.
- Keep include ordering stable and prefer minimal, focused headers.

## Testing Guidelines
- For feature work, add a short manual verification checklist in your PR (load project, import/export assets, run target path).
- For regressions, include a reproducible input asset under `assets/` only when necessary and reasonably small.
- If you add automated tests later, wire them through CMake/CTest and document the run command here.

## Commit & Pull Request Guidelines
- Follow current history style: scoped, imperative summaries such as `Dreamcast: fix NPOT parser crash`.
- Keep commits focused by concern (parser, export path, UI flow), not mixed refactors.
- PRs should include:
  - What changed and why.
  - Validation steps and results.
  - Screenshots or short clips for UI/editor-visible changes.
  - Linked issue/task when applicable.

## Configuration & Safety Notes
- Do not commit local SDK paths or machine-specific environment assumptions.
- Prefer feature flags (`NEBULA_USE_DREAMCAST_SDK`) over hardcoded platform behavior.
