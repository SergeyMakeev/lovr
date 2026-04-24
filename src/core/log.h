#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

// Quark log module
//
// Single funnel for every log line emitted by Quark/LÖVR. Lives in src/core/ and is intentionally
// independent of util.c / api.c / main.c so upstream merges touch it as little as possible.
//
// Output policy:
//   * Each record is formatted ONCE in a central location:
//     `2026-04-23T19:14:03.211Z LEVEL TAG: message\n`
//   * The formatted line is fanned out to every registered sink under a single mutex.
//   * Built-in sinks: stderr, file, Windows OutputDebugString, and a Lua callback bridge.
//
// Threading:
//   * log_printf / log_vprintf are safe to call from any thread.
//   * Sink registration / removal should happen during init (no live re-registration races).
//
// Public C engine code (and the few third-party callbacks we own) MUST go through lovrLog/log_*.
// Direct printf / fprintf / std::cout in src/ is not allowed (CI greps for it).

typedef enum {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR
} log_level;

void log_init(void);
void log_shutdown(void);

void log_set_min_level(log_level level);

// Returns >0 sink handle on success, 0 on failure (too many sinks or open error).
int log_add_stderr_sink(void);
int log_add_file_sink(const char* path);
int log_add_debug_sink(void); // Windows OutputDebugString; no-op + 0 on other platforms

// Removes a previously-registered sink. Closes/frees per-sink resources.
void log_remove_sink(int handle);

// Bridge for the legacy fn_log Lua callback (set by lovrSetLogCallback).
// Implemented in log.c so util.c's lovrSetLogCallback can stay a one-liner.
typedef void fn_log_bridge(void* userdata, int level, const char* tag, const char* format, va_list args);
void log_set_lua_callback(fn_log_bridge* callback, void* userdata);

void log_printf(log_level level, const char* tag, const char* format, ...);
void log_vprintf(log_level level, const char* tag, const char* format, va_list args);

void log_flush(void);
