# Quark runtime fork (Windows)

This repository is a fork of [LÖVR](https://github.com/bjornbytes/lovr) used as a lightweight portable runtime for [Quark](https://github.com/) games on **Windows**. It is **not** intended to track upstream API compatibility.

## Material overrides

Quark’s binding applies per–glTF-material overrides through the instance API:

- `Model:setMaterialOverride(materialIndex, material)` — `materialIndex` is **1-based** (same as `Model:getMaterial`). Pass `nil` to clear.
- `Model:getMaterialOverride(materialIndex)` — returns the override `Material` or `nil`.

Overrides are resolved inside `drawNode` / `lovrPassDrawPart` / `lovrModelGetMesh` via `lovrModelResolvePartMaterial`, so **`pass:draw(model)`** uses the correct materials for skinned rigs without a separate submesh pass.

Implementation: `materialOverrides` array on `struct Model` (parallel to `materials`), set in `graphics.c`; Lua bindings in `src/api/l_graphics_model.c`.

## Building (Windows)

Use the upstream LÖVR build instructions (CMake + MSVC or MinGW). The Windows MSVC build now produces **two binaries** (see [Two-binary model](#two-binary-model-windows)):

- `lovr.exe` — `/SUBSYSTEM:windows`, no console attached, the one you ship to players.
- `lovrc.exe` — `/SUBSYSTEM:console`, real stdio, used for tests, CI, and interactive debugging.

Both binaries embed the same nogame zip postlude and are independently runnable. Replace Quark’s `tools/lovr-win/lovr.exe` (or your pinned path) with the built binary.

---

## Two-binary model (Windows)

Engine logging goes through `src/core/log.{h,c}` (see [Logging](#logging)). To avoid `AttachConsole`/`AllocConsole`/`freopen` gymnastics, the Windows build is split:

| Binary | Subsystem | Default log sinks |
|--------|-----------|-------------------|
| `lovr.exe` | `/SUBSYSTEM:windows` | none (silent unless `--log-file=PATH` is passed). Debug builds also enable an `OutputDebugString` sink. |
| `lovrc.exe` | `/SUBSYSTEM:console` | `stderr` sink. Debug builds also enable an `OutputDebugString` sink. |

Compile-time, `lovrc.exe` sets `LOVR_CONSOLE_BUILD`. This affects two things:

1. `install_default_sinks()` in `main.c` adds the stderr sink only when `LOVR_CONSOLE_BUILD` is defined.
2. `boot.lua` checks `lovr._isConsoleBuild()` and **redirects `_G.print` through `lovr.log`** in `lovr.exe` so user `print()` calls land in the file/debug sinks. In `lovrc.exe`, `print` is left alone — users expect raw stdio writes.

There is no longer any `--console` flag, no `lovr.system.openConsole()`, and no `lovrc.bat` wrapper.

---

## Logging

All engine log output flows through one funnel: `lovrLog(level, tag, format, ...)` (C) → `log_vprintf` in `src/core/log.c`. Every line is formatted **once** as:

```
2026-04-23T19:14:03.211Z LEVEL TAG: message\n
```

…and then dispatched to every registered sink under a single mutex.

### Sinks

Built-in sinks (registered by `main.c` before Lua boots):

- **stderr sink** — added when `LOVR_CONSOLE_BUILD` is defined (i.e. `lovrc.exe`).
- **file sink** — added when `--log-file=PATH` (or `--log-file PATH`) is parsed in `main.c`. The file is opened with `fopen("w")` and `setvbuf(_IONBF)` so writes hit disk immediately.
- **OutputDebugString sink** (Windows, debug builds only).
- **Lua sink** — registered by `lovrSetLogCallback(luax_vlog, T)` in `main.c` once the Lua state exists. Forwards records to the `lovr.log` Lua function for user observation/overlays.

Re-entrancy: `log_vprintf` has a thread-local `dispatch_depth` counter. If `lovr.log` (the user observer) calls back into `lovr._log`/`print`, the recursive `log_vprintf` is a no-op. This is what makes it safe to override `lovr.log` and call `print()` from inside it without infinite recursion or duplicate file writes.

### Discipline

Direct `printf` / `fprintf` / `std::cout` / `puts` in `src/` is not allowed. Use `lovrLog(LOG_LEVEL, "TAG", "fmt", ...)`. Third-party callbacks we own (Vulkan validation `relay`, GLFW `onError`, GPU init diagnostics) all route through the same funnel.

### Lua API

- `lovr.log(message, level, tag)` — Lua-side observer/entry point. Default impl forwards to `lovr._log`. Override to add overlays, network logging, etc.
- `lovr._log(level_string, tag, message)` — direct entry into the engine logger from Lua.
- `lovr._flushLog()` — flushes every sink (file/stderr/debug). Called from `boot.lua` error paths.
- `lovr._isConsoleBuild()` — returns `true` in `lovrc.exe`, `false` in `lovr.exe`.

---

## CI / test mode (Quark harness)

This section documents engine behavior relied on by GPU-less runners, log collection, and short “smoke” sessions.

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
| `--headless` | **Hard headless.** Disables `graphics`, `audio`, `headset` modules and prevents window creation. `system`/`filesystem`/`math`/`physics`/`thread`/`timer` stay loaded. Combine with `lovrc.exe` on Windows so log output is visible. Pre-scanned out of `argv` so it works in any position (e.g. `lovrc test --headless`). |
| `--fatal-errors` | No interactive error overlay; exit with the right code immediately after an uncaught error. |
| `--log-file=PATH` or `--log-file PATH` | Adds a file sink (see [Logging](#logging)) that writes the full-process formatted log to `PATH`. Parsed in `main.c` **before** Lua runs and **before** GPU init, so device-creation failures land in the file. Always additive on top of whatever default sinks the binary registered. |
| `--run-frames=N` | After **N** iterations of the main frame loop, call `lovr.event.quit(0)`. Use for “run a few frames only” CI jobs. |
| `--no-vsync` | Sets `conf.graphics.vsync = false`. |

### `conf.lua` — `conf.test`

Fields under `t.test` (optional): `interactiveErrors`, `fatalErrors`, `runFrames`. CLI runs after `lovr.conf` and overrides.

The log file is CLI-only: pass `--log-file=PATH`. There is no `conf.test.logFile`; the file sink is registered in `main.c` before Lua runs, so there is nothing to configure from Lua after the fact.

### Vulkan / GPU-less (Lavapipe, CI)

- Log lines prefixed with **`LOVR_GPU_INIT:`** — grep-friendly for test runners. They flow through the engine logger now (no raw `fprintf(stderr, ...)`) so they show up in `--log-file` / `OutputDebugString` like everything else.
- Env: **`LOVR_GPULESS`**, **`LOVR_GPU_PREFERENCE`**, **`LOVR_GPU_VERBOSE`**, **`LOVR_SUPPRESS_GPU_DIALOG`** (see implementation in `src/core/gpu_vk.c` and `src/modules/graphics/graphics.c`).

### Log capture in CI

- Full capture: run `lovrc.exe` (so the stderr sink is active by default) and/or pass `--log-file=PATH` (file sink). Both can be combined.
- Engine, Vulkan validation, GLFW errors, Lua `print` (in `lovr.exe`) and `lovr.log` calls all land in the same formatted stream. There is no separate stdio path to lose lines on.
- Not captured: third-party libraries that write to their own log files (Phonon/Jolt — to be wired later) and OS-level traces that never pass through the engine logger.

For a single canonical markdown in this fork, prefer this file over scattered `docs/*` copies.
