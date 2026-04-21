# Quark runtime fork (Windows)

This repository is a fork of [LÖVR](https://github.com/bjornbytes/lovr) used as a lightweight portable runtime for [Quark](https://github.com/) games on **Windows**. It is **not** intended to track upstream API compatibility.

## Material overrides

Quark’s binding applies per–glTF-material overrides through the instance API:

- `Model:setMaterialOverride(materialIndex, material)` — `materialIndex` is **1-based** (same as `Model:getMaterial`). Pass `nil` to clear.
- `Model:getMaterialOverride(materialIndex)` — returns the override `Material` or `nil`.

Overrides are resolved inside `drawNode` / `lovrPassDrawPart` / `lovrModelGetMesh` via `lovrModelResolvePartMaterial`, so **`pass:draw(model)`** uses the correct materials for skinned rigs without a separate submesh pass.

Implementation: `materialOverrides` array on `struct Model` (parallel to `materials`), set in `graphics.c`; Lua bindings in `src/api/l_graphics_model.c`.

## Building (Windows)

Use the upstream LÖVR build instructions (CMake + MSVC or MinGW). Replace Quark’s `tools/lovr-win/lovr.exe` (or your pinned path) with the built binary.

---

## CI / test mode (Quark harness)

This section documents engine behavior relied on by GPU-less runners, log collection, and short “smoke” sessions. Implementation follows `PLAN_quark_ci_test_mode.md`.

### Process exit codes

| Code | Meaning |
|------|---------|
| `0` | Normal exit (`lovr.event.quit()` or `--run-frames` completed). |
| `1` | Uncaught Lua error (after traceback handling). |
| `2` | Graphics / GPU init or window creation failure (when classified by error text). |
| `3` | Failure to load or execute embedded `boot.lua` in the C entry point (`main`). |
| `4` | Reserved (e.g. future engine-owned timeouts). |

### CLI flags (default `etc/nogame/arg.lua`)

| Flag | Effect |
|------|--------|
| `--fatal-errors` | No interactive error overlay; exit with the right code immediately after an uncaught error. |
| `--log-file=PATH` or `--log-file PATH` | **Full-process log:** applied in **`main.c` before Lua** — **stdout and stderr** are redirected to `PATH`, so output from **C, Lua `print`, `lovr.log`, and anything using stdio** goes into one file. Sets `LOVR_STDIO_LOG=1` for the runtime. Use this form on the **command line** for complete capture; essential for CI stability. |
| `--run-frames=N` | After **N** iterations of the main frame loop, call `lovr.event.quit(0)`. Use for “run a few frames only” CI jobs (e.g. load + draw smoke). |
| `--no-vsync` | Sets `conf.graphics.vsync = false`. |

### `conf.lua` — `conf.test`

Fields under `t.test` (optional): `interactiveErrors`, `fatalErrors`, `logFile`, `runFrames`. CLI runs after `lovr.conf` and overrides.

**Log file caveat:** Full stdout/stderr redirection only happens when **`--log-file` is present on the process argv** (handled in C). If you set **only** `conf.test.logFile` in Lua without that CLI flag, the runtime uses a **Lua-only** tee (`print` + `lovr.log`) and does **not** retroactively capture early C output.

### Vulkan / GPU-less (Lavapipe, CI)

- Stderr lines prefixed with **`LOVR_GPU_INIT:`** — grep-friendly for test runners.
- Env: **`LOVR_GPULESS`**, **`LOVR_GPU_PREFERENCE`**, **`LOVR_GPU_VERBOSE`**, **`LOVR_SUPPRESS_GPU_DIALOG`** (see implementation in `src/core/gpu_vk.c` and `src/modules/graphics/graphics.c`).

### What counts as “100%” log capture

- **Included** when using **`--log-file=...`**: normal **stdio** (printf, `fprintf`, Lua `print`, typical engine logging that goes through stdout/stderr).
- **Not guaranteed**: `OutputDebugString` / unrelated OS tracing, kernel/driver logs that never pass through the process stdio, or other side channels.

For a single canonical markdown in this fork, prefer this file over scattered `docs/*` copies.
