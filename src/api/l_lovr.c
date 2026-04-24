#include "api.h"
#include "core/log.h"
#include "util.h"
#include <string.h>

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

// Flushes every registered log sink (file, stderr, debug). Called from boot.lua's error path
// so logs are guaranteed to hit disk before exit/crash.
static int l_lovrFlushLog(lua_State* L) {
  log_flush();
  return 0;
}

// `lovr._log(level_string, tag, message)` — direct entry into the engine logger from Lua. Used by
// the default `lovr.log` and (in lovr.exe) the `print` redirect. Re-entrant calls are no-ops
// thanks to the dispatch_depth guard in src/core/log.c, so it's safe to call from within a
// `lovr.log` observer.
static int l_lovrLogEmit(lua_State* L) {
  const char* level_str = luaL_optstring(L, 1, "info");
  const char* tag = lua_isnoneornil(L, 2) ? NULL : lua_tostring(L, 2);
  const char* message = luaL_optstring(L, 3, "");

  log_level level = LOG_LEVEL_INFO;
  if      (!strcmp(level_str, "debug")) level = LOG_LEVEL_DEBUG;
  else if (!strcmp(level_str, "info"))  level = LOG_LEVEL_INFO;
  else if (!strcmp(level_str, "warn"))  level = LOG_LEVEL_WARN;
  else if (!strcmp(level_str, "error")) level = LOG_LEVEL_ERROR;

  log_printf(level, tag, "%s", message);
  return 0;
}

// `lovr._consoleBuild` reflects whether this is the console-subsystem build (lovrc.exe). boot.lua
// uses it to decide whether to redirect Lua `print` through `lovr.log` (yes for lovr.exe so prints
// reach the file/debug sinks; no for lovrc.exe where users expect raw stderr).
static int l_lovrIsConsoleBuild(lua_State* L) {
#ifdef LOVR_CONSOLE_BUILD
  lua_pushboolean(L, 1);
#else
  lua_pushboolean(L, 0);
#endif
  return 1;
}

static int l_lovrGetVersion(lua_State* L) {
  lua_pushinteger(L, LOVR_VERSION_MAJOR);
  lua_pushinteger(L, LOVR_VERSION_MINOR);
  lua_pushinteger(L, LOVR_VERSION_PATCH);
  lua_pushliteral(L, LOVR_VERSION_ALIAS);
#ifdef LOVR_VERSION_HASH
  lua_pushstring(L, STRINGIFY(LOVR_VERSION_HASH));
  return 5;
#else
  return 4;
#endif
}

static const luaL_Reg lovr[] = {
  { "_setConf", luax_setconf },
  { "_flushLog", l_lovrFlushLog },
  { "_log", l_lovrLogEmit },
  { "_isConsoleBuild", l_lovrIsConsoleBuild },
  { "getVersion", l_lovrGetVersion },
  { NULL, NULL }
};

int luaopen_lovr(lua_State* L) {
  lua_newtable(L);
  luax_register(L, lovr);
  return 1;
}
