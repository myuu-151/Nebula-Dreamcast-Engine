# Nebula Dependencies and Path Requirements

This doc lists the core tooling needed for Nebula development (Windows editor + Dreamcast build), with practical path requirements and troubleshooting notes.

---

## 1) Core Dependencies

## A) Visual Studio Build Tools / Native Tools x64

Used for:
- compiling Windows-side runtime/script DLLs (`cl.exe`)
- C/C++ build tasks that rely on MSVC toolchain

Required components (recommended):
- MSVC v143 (or matching project toolset)
- Windows SDK
- x64 Native Tools command environment

Common check:
```bat
where cl
```
If not found, use Developer Command Prompt or ensure `vcvarsall.bat` path is resolvable.

Typical vcvars path:
- `C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`

---

## B) CMake + CMake GUI

Used for:
- configuring/generating build files for parts of the project
- dependency setup and local build orchestration

Recommended:
- Install CMake and add to PATH
- Keep CMake GUI installed for quick cache/path fixes

Checks:
```bat
cmake --version
where cmake
```

---

## C) DreamSDK / KallistiOS toolchain (Dreamcast)

Used for:
- `sh-elf-gcc`, `kos-cc`, KOS headers/libs
- packaging Dreamcast ELF + CDI

Expected tools include:
- `sh-elf-gcc`
- `kos-cc`
- `mkisofs`
- `cdi4dc`

Checks:
```bat
where sh-elf-gcc
where kos-cc
```

Typical KOS base used by Makefiles:
- `/c/DreamSDK/opt/toolchains/dc/kos`

---

## D) Git

Used for:
- source sync, branching, patch/cherry-pick workflows

Checks:
```bat
git --version
git remote -v
```

---

## E) Python (optional but useful)

Used for helper scripts/tooling where applicable.

Check:
```bat
python --version
```

---

## 2) Project Path Requirements

For current workflow, important paths include:

- Active Dreamcast engine source (example):
  - `C:\Users\NoSig\Documents\Dreamcast\ryan_nebula\Nebula-Dreamcast-Engine-main`

- Active build output (example):
  - `C:\Users\NoSig\Desktop\nebulaproj\build_dreamcast`

- Dreamcast bindings source:
  - `<engine>\src\platform\dreamcast\KosBindings.c/.h`
  - `<engine>\src\platform\dreamcast\KosInput.c/.h`

- Scripts (source/project):
  - `<engine>\Scripts\*.c`

- Scripts (staged build copy used by DC build):
  - `<build_dreamcast>\scripts\*.c`

> Important: if source and staged scripts diverge, runtime behavior can differ from expectations.

---

## 3) Dreamcast Makefile Expectations

`build_dreamcast/Makefile.dreamcast` should define:

- `NEBULA_DC_BINDINGS` path pointing to the intended engine `src/platform/dreamcast`
- `SOURCES` including runtime and bindings:
  - `main.c`
  - `KosBindings.c`
  - `KosInput.c`
  - script sources (`scripts/*.c` if auto-pick enabled)
  - `NebulaGameStub.c`

Example pattern:
```make
SCRIPT_SOURCES = $(wildcard scripts/*.c)
SOURCES = main.c KosBindings.c KosInput.c $(SCRIPT_SOURCES) NebulaGameStub.c
```

---

## 4) Environment and PATH Notes

For reliable builds, ensure these are discoverable:

- MSVC tools (`cl.exe`) for Windows script compile flows
- DreamSDK tools (`sh-elf-gcc`, `kos-cc`) for Dreamcast build
- CMake tools (`cmake`) for generation/config steps

When build behavior is inconsistent, validate tool resolution from the exact shell used by Nebula.

---

## 5) Common Failure Modes

## A) `cl.exe not found`
Cause:
- Native Tools environment not loaded
Fix:
- launch from VS Native Tools prompt or run `vcvarsall.bat x64`

## B) Dreamcast link errors for `NB_DC_*` / `NB_KOS_*`
Cause:
- `KosBindings.c` / `KosInput.c` missing from `SOURCES`
- wrong `NEBULA_DC_BINDINGS` path
Fix:
- correct Makefile path and source list

## C) Script works in editor but not Dreamcast
Cause:
- staged `build_dreamcast/scripts/*.c` is stale
- script object not linked
Fix:
- sync script copy and verify compile line includes `scripts/<name>.c`

## D) Multiple repos with similar names causing wrong-source builds
Cause:
- editing one clone, building another
Fix:
- print and verify absolute paths in build logs/config

---

## 6) Recommended Validation Commands

From Windows shell:
```bat
where cl
where sh-elf-gcc
where kos-cc
cmake --version
git --version
```

From Dreamcast build folder:
```bat
type Makefile.dreamcast
```
Verify `NEBULA_DC_BINDINGS` and `SOURCES` match your intended source tree.

---

## 7) Suggested Operational Practices

- Keep one clear **source-of-truth repo** per active task.
- Document active build path in project notes.
- Regenerate runtime (`main.c`, Makefile) after major generator edits.
- Prefer wrapper APIs (`NB_KOS_*`) over raw platform bits in scripts.
- Add lightweight startup debug logs to confirm script hook execution.

---

## 8) Optional Quality-of-Life Tools

- `ninja` (faster CMake builds)
- `ripgrep` (`rg`) for code search
- serial/log viewer for Dreamcast output

---

## 9) Minimum Working Stack (quick checklist)

- [ ] Visual Studio Build Tools (x64 C++ tools)
- [ ] CMake (+ CMake GUI)
- [ ] DreamSDK with KOS toolchain
- [ ] Git
- [ ] Correct `NEBULA_DC_BINDINGS` path
- [ ] `KosBindings.c` + `KosInput.c` in Dreamcast `SOURCES`
- [ ] Script `.c` files included in `SOURCES`
