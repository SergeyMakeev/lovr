#include "api/api.h"
#include "core/os.h"
#include "util.h"
#include "boot.lua.h"
#include <lualib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

// If argv contains `--log-file=PATH` (or `--log-file PATH`), redirect stdout and stderr to PATH
// so the log captures C output (driver/GPU init messages), Lua `print`, and Lua
// `io.stdout:write` / `io.stderr:write` in one place.
//
// Two things matter for this to behave correctly:
//
// 1. `freopen` (not `dup2`) is used to attach the CRT streams. On Windows, lovr links as
//    `/SUBSYSTEM:windows` in Release: `stdout` / `stderr` start with no OS handle and `dup2`
//    can't target an fd that's never been opened.
//
// 2. After freopen, stdout and stderr own two DIFFERENT OS-level file objects that happen to
//    point at the same path. On Windows each object tracks its own write position, so writes
//    from one stream overwrite bytes already written by the other. We fix this by using
//    `_dup2` / `dup2` to point stderr's fd at stdout's fd, so both CRT streams share a single
//    underlying kernel file description and one write position.
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
  setvbuf(stdout, NULL, _IONBF, 0);

  // Give stderr a valid fd first. In a Windows GUI-subsystem build stderr has no OS handle at
  // startup and `_fileno(stderr)` returns an invalid slot that `_dup2` can't use as a target.
  if (!freopen(path, "a", stderr)) {
    return;
  }
  setvbuf(stderr, NULL, _IONBF, 0);

#ifdef _WIN32
  int sofd = _fileno(stdout);
  int sefd = _fileno(stderr);
  if (sofd >= 0 && sefd >= 0 && sofd != sefd) {
    _dup2(sofd, sefd);
  }
#else
  int sofd = fileno(stdout);
  int sefd = fileno(stderr);
  if (sofd >= 0 && sefd >= 0 && sofd != sefd) {
    dup2(sofd, sefd);
  }
#endif
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
