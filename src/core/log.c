#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ---- record + sinks -----------------------------------------------------------------------------

typedef struct {
  log_level level;
  const char* tag;            // may be NULL
  const char* message;        // unformatted message body
  const char* line;           // fully formatted line including trailing \n
  size_t line_length;
} log_record;

typedef enum {
  SINK_NONE = 0,
  SINK_STDERR,
  SINK_FILE,
  SINK_DEBUG,   // Windows OutputDebugString
  SINK_LUA      // bridge to Lua-level callback registered via lovrSetLogCallback
} sink_kind;

typedef struct {
  int handle;        // 0 means slot is free
  sink_kind kind;
  FILE* file;        // SINK_FILE only
} sink_entry;

#define LOG_MAX_SINKS 8
#define LOG_LINE_MAX 4096

static struct {
  bool initialized;
  mtx_t mutex;
  log_level min_level;
  int next_handle;
  sink_entry sinks[LOG_MAX_SINKS];

  fn_log_bridge* lua_cb;
  void* lua_ud;
} state;

// Re-entrancy guard. Prevents `lovr.log` (the Lua observer) from looping back into log_vprintf.
// Scenario: a C-side lovrLog dispatches to SINK_LUA, which calls `lovr.log` Lua function, whose
// default impl calls `lovr._log` which calls log_printf again. Without this guard the second
// emission would write duplicate lines to file/stderr/debug AND recurse into the Lua observer
// indefinitely. With this guard, a re-entrant log_vprintf is a no-op.
static thread_local int dispatch_depth;

static const char* level_name(log_level level) {
  switch (level) {
    case LOG_LEVEL_DEBUG: return "DEBUG";
    case LOG_LEVEL_INFO:  return "INFO ";
    case LOG_LEVEL_WARN:  return "WARN ";
    case LOG_LEVEL_ERROR: return "ERROR";
    default:              return "?    ";
  }
}

// ---- init / shutdown ----------------------------------------------------------------------------

void log_init(void) {
  if (state.initialized) return;
  mtx_init(&state.mutex, mtx_plain);
  state.min_level = LOG_LEVEL_DEBUG;
  state.next_handle = 1;
  state.initialized = true;
}

void log_shutdown(void) {
  if (!state.initialized) return;

  mtx_lock(&state.mutex);
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle && state.sinks[i].kind == SINK_FILE && state.sinks[i].file) {
      fflush(state.sinks[i].file);
      fclose(state.sinks[i].file);
    }
    state.sinks[i] = (sink_entry) { 0 };
  }
  state.lua_cb = NULL;
  state.lua_ud = NULL;
  mtx_unlock(&state.mutex);

  mtx_destroy(&state.mutex);
  state.initialized = false;
}

void log_set_min_level(log_level level) {
  state.min_level = level;
}

// ---- sink registration --------------------------------------------------------------------------

static int allocate_sink(sink_kind kind) {
  log_init();
  mtx_lock(&state.mutex);
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle == 0) {
      int h = state.next_handle++;
      if (state.next_handle <= 0) state.next_handle = 1; // wrap guard
      state.sinks[i] = (sink_entry) { .handle = h, .kind = kind, .file = NULL };
      mtx_unlock(&state.mutex);
      return h;
    }
  }
  mtx_unlock(&state.mutex);
  return 0;
}

int log_add_stderr_sink(void) {
  return allocate_sink(SINK_STDERR);
}

int log_add_file_sink(const char* path) {
  if (!path || !path[0]) return 0;
  FILE* f = fopen(path, "w");
  if (!f) return 0;
  setvbuf(f, NULL, _IONBF, 0);

  int handle = allocate_sink(SINK_FILE);
  if (!handle) { fclose(f); return 0; }

  mtx_lock(&state.mutex);
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle == handle) {
      state.sinks[i].file = f;
      break;
    }
  }
  mtx_unlock(&state.mutex);
  return handle;
}

int log_add_debug_sink(void) {
#ifdef _WIN32
  return allocate_sink(SINK_DEBUG);
#else
  return 0;
#endif
}

void log_remove_sink(int handle) {
  if (!handle || !state.initialized) return;
  mtx_lock(&state.mutex);
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle == handle) {
      if (state.sinks[i].kind == SINK_FILE && state.sinks[i].file) {
        fflush(state.sinks[i].file);
        fclose(state.sinks[i].file);
      }
      state.sinks[i] = (sink_entry) { 0 };
      break;
    }
  }
  mtx_unlock(&state.mutex);
}

void log_set_lua_callback(fn_log_bridge* callback, void* userdata) {
  log_init();
  mtx_lock(&state.mutex);
  state.lua_cb = callback;
  state.lua_ud = userdata;

  // Ensure exactly one SINK_LUA slot exists when a callback is registered.
  bool found = false;
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle && state.sinks[i].kind == SINK_LUA) {
      found = true;
      if (!callback) {
        state.sinks[i] = (sink_entry) { 0 };
        found = false;
      }
      break;
    }
  }
  if (callback && !found) {
    for (int i = 0; i < LOG_MAX_SINKS; i++) {
      if (state.sinks[i].handle == 0) {
        int h = state.next_handle++;
        if (state.next_handle <= 0) state.next_handle = 1;
        state.sinks[i] = (sink_entry) { .handle = h, .kind = SINK_LUA, .file = NULL };
        break;
      }
    }
  }
  mtx_unlock(&state.mutex);
}

// ---- formatting + dispatch ----------------------------------------------------------------------

// Format `2026-04-23T19:14:03.211Z` from current wall clock into `out` (>= 25 bytes including NUL).
static void format_timestamp(char* out, size_t cap) {
  if (cap < 25) { if (cap) out[0] = '\0'; return; }

  struct timespec ts;
  if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
    out[0] = '\0';
    return;
  }

  struct tm tmv;
#ifdef _WIN32
  gmtime_s(&tmv, &ts.tv_sec);
#else
  gmtime_r(&ts.tv_sec, &tmv);
#endif

  int ms = (int) (ts.tv_nsec / 1000000);
  snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
    tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
    tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
}

// Trims a trailing '\n' off `msg`. Returns new length.
static size_t trim_trailing_newline(char* msg, size_t len) {
  while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
    msg[--len] = '\0';
  }
  return len;
}

static void invoke_lua_bridge(fn_log_bridge* cb, void* ud, int level, const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  cb(ud, level, tag, fmt, args);
  va_end(args);
}

static void dispatch_to_sink(const sink_entry* sink, const log_record* rec) {
  switch (sink->kind) {
    case SINK_STDERR:
      fwrite(rec->line, 1, rec->line_length, stderr);
      fflush(stderr);
      break;

    case SINK_FILE:
      if (sink->file) {
        fwrite(rec->line, 1, rec->line_length, sink->file);
        // file is unbuffered (set in log_add_file_sink), so writes hit disk immediately.
      }
      break;

#ifdef _WIN32
    case SINK_DEBUG:
      OutputDebugStringA(rec->line);
      break;
#endif

    case SINK_LUA:
      if (state.lua_cb) {
        // Bridge to the legacy fn_log signature (format + va_list). The callback typically calls
        // lua_pushvfstring(L, format, args); pass the already-formatted message as a literal "%s"
        // so the callback just receives the final string.
        invoke_lua_bridge(state.lua_cb, state.lua_ud, (int) rec->level, rec->tag, "%s", rec->message);
      }
      break;

    default:
      break;
  }
}

void log_vprintf(log_level level, const char* tag, const char* format, va_list args) {
  log_init();

  if (level < state.min_level) return;
  if (dispatch_depth > 0) return; // re-entrant call from a Lua observer; bail
  dispatch_depth++;

  char message[LOG_LINE_MAX];
  int n = vsnprintf(message, sizeof(message), format ? format : "", args);
  if (n < 0) {
    message[0] = '\0';
    n = 0;
  } else if ((size_t) n >= sizeof(message)) {
    n = (int) sizeof(message) - 1;
  }
  size_t msg_len = trim_trailing_newline(message, (size_t) n);

  char timestamp[32];
  format_timestamp(timestamp, sizeof(timestamp));

  char line[LOG_LINE_MAX + 128];
  int line_len;
  if (tag && tag[0]) {
    line_len = snprintf(line, sizeof(line), "%s %s %s: %s\n",
      timestamp, level_name(level), tag, message);
  } else {
    line_len = snprintf(line, sizeof(line), "%s %s: %s\n",
      timestamp, level_name(level), message);
  }
  if (line_len < 0) return;
  if ((size_t) line_len >= sizeof(line)) line_len = (int) sizeof(line) - 1;

  log_record rec = {
    .level = level,
    .tag = tag,
    .message = message,
    .line = line,
    .line_length = (size_t) line_len
  };

  (void) msg_len;

  mtx_lock(&state.mutex);
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle) {
      dispatch_to_sink(&state.sinks[i], &rec);
    }
  }
  mtx_unlock(&state.mutex);

  dispatch_depth--;
}

void log_printf(log_level level, const char* tag, const char* format, ...) {
  va_list args;
  va_start(args, format);
  log_vprintf(level, tag, format, args);
  va_end(args);
}

void log_flush(void) {
  if (!state.initialized) return;
  mtx_lock(&state.mutex);
  fflush(stderr);
  for (int i = 0; i < LOG_MAX_SINKS; i++) {
    if (state.sinks[i].handle == 0) continue;
    if (state.sinks[i].kind == SINK_FILE && state.sinks[i].file) {
      fflush(state.sinks[i].file);
    }
  }
  mtx_unlock(&state.mutex);
}
