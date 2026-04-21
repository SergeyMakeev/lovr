# Plan: LOVR CI / test mode (Quark-aligned fork)

This document is the implementation plan for engine-level support that [Quark](https://github.com/SergeyMakeev/Quark) (and similar projects) currently approximate with bootstrap Lua (`errhand`, `print` tee), Python stderr sidecars, and documentation. Another agent can implement from this file.

## Goals

- **CI-first:** predictable process exit, logs on disk, no interactive error UI, GPU-less runners (Lavapipe) fail with clear diagnostics where the engine can help.
- **Game-optional:** minimal or no per-project `lovr.errhand` / `print` monkey-patches for standard harness behavior.
- **Quark alignment (later):** shrink Quark’s `src/Bootstrap/LOVR/main.lua` and `build/test_runner.py` / `build/vistest_runner.py` to set official LOVR flags instead of reimplementing behavior.

## Non-goals (initial scope)

- Full **true headless** with zero windowing (only if the architecture allows without a large GLFW refactor); treat as stretch.
- Bit-identical GPU frames across machines; focus on **controlled run** (fixed window size, bounded frames, explicit quit).

---

## Phase 0 — Spec and contracts (do first)

Document in this repo (e.g. `docs/TEST_MODE.md` or a section in existing docs):

### Exit code table (sketch — align with LOVR maintainers)

| Code | Meaning |
|------|--------|
| `0` | Normal quit (`lovr.event.quit()` or run mode completed). |
| `1` | Uncaught Lua error / `error()` after configured traceback handling. |
| `2` | Graphics or window creation failure (Vulkan/GLFW/init). |
| `3` | CLI / `conf.lua` / early module load failure. |
| `4` | Reserved: run-mode timeout or abnormal exhaustion (if engine owns timeouts later). |
| `≥128` | Optional: platform-specific reserved. |

### Orthogonal modes

- **Non-interactive:** no error overlay; no blocking “press key” UI.
- **Fatal Lua:** immediate process exit on first uncaught error; optional full traceback to log.
- **Log file:** path + tee vs redirect + flush rules (see Phase 2).
- **Run mode:** bounded frames / fixed-step options (see Phase 3).
- **GPU / CI presets:** optional `--graphics-preset=ci` or env-driven hints (see Phase 4).

### Log scope

Which streams go to the log file: Lua `print`, `warn`, `lovr.errhand`, and **best-effort** C `stderr` duplication (document Windows limitations).

---

## Phase 1 — Non-interactive errors + fatal Lua + exit codes

### Implementation

- Add configuration surface, e.g.:
  - `lovr.conf` table: `t.test` or `t.ci` with `fatalErrors`, `interactiveErrors`, or
  - CLI flags (often better for CI without editing every game’s `conf.lua`): e.g. `--fatal-errors`, `--no-error-screen`.
- Centralize uncaught error handling so all paths:
  - write through the log sink when configured (Phase 2),
  - set the **process exit code** per the table,
  - skip the interactive error screen when non-interactive.

### Acceptance

- Minimal project with deliberate `error("x")`: no blocking UI, exit code `1`, traceback available in log/stderr per spec.

### Downstream (Quark, later)

- Remove or gate Quark’s custom `lovr.errhand` in `src/Bootstrap/LOVR/main.lua` when fork provides equivalent behavior.
- Keep `QUARK_TEST_*` log markers only if still needed for structured parsing, or replace with LOVR-native markers.

---

## Phase 2 — Official log file

### Implementation

- Single supported option, e.g. `--log-file=PATH` or `t.test.logFile = "..."`.
- Choose and document **tee vs redirect** (CI often wants tee to console + file).
- **Flush:** per-line or per `print`/`warn`; **full flush** on fatal path before `exit`.
- Optional: `--log-env` or narrow env dump at boot (`VK_*` only) for Lavapipe debugging; off by default to avoid leaking secrets.

### Acceptance

- Lua-only failure produces a complete traceback in the log file without relying on a host-process stderr sidecar.

### Downstream (Quark, later)

- Replace `QUARK_TEST_OUTPUT` print tee with engine flag set from `build/test_runner.py` / env bridge.
- Retain Python `lovr_stderr.log` sidecar only until C stderr merge is verified on Windows.

---

## Phase 3 — Deterministic / bounded run (“run mode”)

### Implementation

- **Fixed frame count:** e.g. `--run-frames=N` — after N update/render/present cycles (define the exact hook in LOVR’s loop), quit with `0`.
- Optional: **fixed timestep** / reduced wall-clock sensitivity for tests (document what still uses real time: audio, threads, etc.).
- Optional: **vsync / FPS cap** for CI via conf or flag (complements host env like Quark’s `QUARK_LOVR_WINDOW_*`).

### Acceptance

- Minimal project with no game-driven `quit` exits `0` when `--run-frames=1` (or equivalent).

### Downstream (Quark, later)

- vistest / screenshot: combine `run-frames` with fixed window dimensions.
- Unit test smoke: “loads and exits 0” without custom bootstrap.

---

## Phase 4 — Lavapipe / GPU-less builders

### Implementation

- On Vulkan (or graphics) init failure: print a **single actionable block** mentioning missing ICD, `VK_ICD_FILENAMES` / `VK_DRIVER_FILES`, and pointer to a short “Windows CI” recipe (e.g. Mesa Lavapipe, jakoch action).
- Map common failures to exit code **`2`** with a **stable stderr/log prefix** (grep-friendly).
- Optional: `--graphics-preset=ci` for conservative defaults (resolution, vsync off); avoid slowing default builds.

### Acceptance

- Misconfigured Vulkan on a headless runner: clear message + exit `2`, not an opaque crash where avoidable.

### Downstream (Quark, later)

- Shorten `docs/ci-lovr-mesa.md` to env setup; LOVR owns “what to do when init fails.”

---

## Phase 5 — Quark integration and deprecation

1. Ship new `lovr.exe` + DLLs in Quark `tools/lovr-win/` from this fork.
2. `build/test_runner.py`, `build/vistest_runner.py`: pass new CLI flags; keep stderr sidecar until Phase 2 proven on Windows.
3. `src/Bootstrap/LOVR/main.lua`: delete duplicated `errhand` / `print` tee when fork covers behavior; keep `QUARK_*` env for **one release** if backward compatibility matters.
4. Update Quark `docs/unit-tests.md` (or equivalent) with the exit-code contract.

---

## Phase 6 — Upstream and versioning

- Consider a **minimal upstream PR** (non-interactive + exit codes + log file) separate from Quark-only rendering patches.
- Expose version or capability bit so Quark can `require` fork ≥ N (e.g. `lovr.getVersion()` patch or compile-time define).

---

## Suggested implementation order

1. Exit codes + non-interactive + fatal Lua.  
2. Official log file (fatal-path flush).  
3. `--run-frames` / bounded run.  
4. Lavapipe diagnostics + optional CI preset.  
5. Quark deletion pass + docs.

---

## Risks

- **Windows:** merging C `stderr` with Lua log may be imperfect; keep host sidecar as fallback until verified.
- **Determinism:** scope is “controlled run,” not identical pixels across GPUs.
- **Compatibility:** keep Quark’s old env vars for one release if external workflows depend on them.

---

## Reference: what Quark does today (to replace)

- `src/Bootstrap/LOVR/main.lua`: `QUARK_TEST_OUTPUT` tees `print`; custom `lovr.errhand` writes markers and `os.exit(1)` in test mode.
- `build/lovr_log_monitor.py`, `build/test_runner.py`: poll log + `lovr_stderr.log` from `Popen(stderr=...)`.
- `docs/ci-lovr-mesa.md`: Lavapipe env on Windows CI.

---

*Written for handoff to an implementation agent. Path: `lovr/PLAN_quark_ci_test_mode.md`.*
