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
#include <fcntl.h>
#endif

// If argv contains `--log-file=PATH` (or `--log-file` + PATH), redirect both stdout and stderr
// to that file so C, Lua print, and driver messages that go through stdio land in one place.
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

#ifdef _WIN32
  FILE* log = fopen(path, "wb");
  if (!log) {
    return;
  }
  int logfd = _fileno(log);
  fflush(stdout);
  fflush(stderr);
  if (_dup2(logfd, _fileno(stdout)) < 0 || _dup2(logfd, _fileno(stderr)) < 0) {
    return;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#else
  fflush(stdout);
  fflush(stderr);
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return;
  }
  if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
    close(fd);
    return;
  }
  if (fd > 2) {
    close(fd);
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif

#if defined(_WIN32)
  _putenv_s("LOVR_STDIO_LOG", "1");
#else
  setenv("LOVR_STDIO_LOG", "1", 1);
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
      luax_close(L);
      continue;
    } else {
      int status = lua_tointeger(T, 1);
      luax_close(L);
      os_destroy();
      return status;
    }
  }
}
