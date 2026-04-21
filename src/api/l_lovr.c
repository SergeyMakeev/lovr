#include "api.h"
#include "util.h"
#include <stdio.h>

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

// Ensures stderr/stdout hit disk when --log-file redirected stdio (Lua io.flush is not always enough).
static int l_lovrFlushStdio(lua_State* L) {
  fflush(stdout);
  fflush(stderr);
  return 0;
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
  { "_flushStdio", l_lovrFlushStdio },
  { "getVersion", l_lovrGetVersion },
  { NULL, NULL }
};

int luaopen_lovr(lua_State* L) {
  lua_newtable(L);
  luax_register(L, lovr);
  return 1;
}
