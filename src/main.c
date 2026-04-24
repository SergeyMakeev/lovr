#include "api/api.h"
#include "core/log.h"
#include "core/os.h"
#include "util.h"
#include "boot.lua.h"
#include <lualib.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// Quark log/console design (Windows in particular):
//   * Two executables: `lovr.exe` (/SUBSYSTEM:windows, no console) and `lovrc.exe`
//     (/SUBSYSTEM:console, real stdio). Both link the same code; behaviour differs only via
//     LOVR_CONSOLE_BUILD.
//   * No `freopen`/`_dup2` stdio juggling. No `AttachConsole`/`AllocConsole`. No `--console` flag.
//     Instead, every log line in the engine flows through src/core/log.{h,c} which fans the
//     formatted record out to whatever sinks were registered.
//   * Default sinks per binary:
//       - `lovrc.exe`         -> stderr sink
//       - `lovr.exe`          -> none (silent unless `--log-file=...` is passed)
//       - Debug builds (both) -> additionally an OutputDebugString sink on Windows
//   * `--log-file=PATH` (or `--log-file PATH`) is parsed below — before Lua runs and before
//     graphics initialises — so even GPU init failures land in the file.

static const char* find_log_file_arg(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--log-file=", 11) == 0 && argv[i][11]) {
      return argv[i] + 11;
    }
    if (!strcmp(argv[i], "--log-file") && i + 1 < argc && argv[i + 1][0] != '-') {
      return argv[i + 1];
    }
  }
  return NULL;
}

static void install_default_sinks(void) {
  log_init();

#ifdef LOVR_CONSOLE_BUILD
  log_add_stderr_sink();
#endif

#if defined(_WIN32) && !defined(NDEBUG)
  log_add_debug_sink();
#endif
}

int main(int argc, char** argv) {
  install_default_sinks();

  const char* log_file_path = find_log_file_arg(argc, argv);
  if (log_file_path) {
    log_add_file_sink(log_file_path);
  }

  os_init();

  for (;;) {
    lua_State* L = luaL_newstate();
    luax_setmainthread(L);
    luaL_openlibs(L);
    luax_preload(L);

    lua_newtable(L);
    static Variant cookie;
    luax_pushvariant(L, &cookie);
    lua_setfield(L, -2, "restart");
    for (int i = 0; i < argc; i++) {
      lua_pushstring(L, argv[i]);
      lua_rawseti(L, -2, i);
    }
    lua_setglobal(L, "arg");

    lua_pushcfunction(L, luax_getstack);
    int status = luax_loadbufferx(L, (const char*) etc_boot_lua, etc_boot_lua_len, "@boot.lua", NULL);
    if (status != 0 || lua_pcall(L, 0, 1, -2)) {
      lovrLog(LOG_ERROR, "boot", "%s", lua_tostring(L, -1));
      log_flush();
      os_destroy();
      log_shutdown();
      return 3;
    }

    lua_State* T = lua_tothread(L, -1);
    lovrSetLogCallback(luax_vlog, T);

    while (luax_resume(T, 0) == LUA_YIELD) {
      os_sleep(0.);
    }

    if (lua_type(T, 1) == LUA_TSTRING && !strcmp(lua_tostring(T, 1), "restart")) {
      luax_checkvariant(T, 2, &cookie);
      if (cookie.type == TYPE_OBJECT) memset(&cookie, 0, sizeof(cookie));
      log_flush();
      // Detach the Lua sink before tearing down the lua_State, so any logging that happens during
      // module destruction or the next state's bring-up doesn't reenter a freed thread.
      lovrSetLogCallback(NULL, NULL);
      luax_close(L);
      continue;
    } else {
      int status = lua_tointeger(T, 1);
      log_flush();
      lovrSetLogCallback(NULL, NULL);
      luax_close(L);
      os_destroy();
      log_shutdown();
      return status;
    }
  }
}
