#include "api/api.h"
#include "core/os.h"
#include "util.h"
#include "boot.lua.h"
#include <lualib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// If argv contains `--log-file=PATH` (or `--log-file` + PATH), redirect both stdout and stderr
// to that file so C, Lua print, and driver messages that go through stdio land in one place.
//
// Uses freopen rather than dup2 because on Windows lovr links as `/SUBSYSTEM:windows` (GUI app)
// in Release: there is no console and `_fileno(stdout)` is invalid, so dup2 silently fails and
// nothing ends up in the log. freopen reassociates the FILE* stream with a fresh fd regardless
// of whether the original stream was attached to anything.
static void lovr_stdio_log_from_argv(int argc, char** argv) {
  const char* path = NULL;
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--log-file=", 11) == 0 && argv[i][11]) {
      path = argv[i] + 11;
      break;
    }
    if (!strcmp(argv[i], "--log-file") && i + 1 < argc && argv[i + 1][0] != '-') {
      path = argv[++i];
      break;
    }
  }
  if (!path || !path[0]) {
    return;
  }

  if (!freopen(path, "w", stdout)) {
    return;
  }

  // Point stderr at the same file so a single tail -f shows everything in order.
  // If freopen on stderr fails for any reason, we still have stdout redirected.
  freopen(path, "a", stderr);

  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
}

int main(int argc, char** argv) {
  os_init();
  lovr_stdio_log_from_argv(argc, argv);

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
      fprintf(stderr, "%s\n", lua_tostring(L, -1));
      fflush(stderr);
      fflush(stdout);
      os_destroy();
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
      fflush(stdout);
      fflush(stderr);
      luax_close(L);
      continue;
    } else {
      int status = lua_tointeger(T, 1);
      fflush(stdout);
      fflush(stderr);
      luax_close(L);
      os_destroy();
      return status;
    }
  }
}
