/* Compile Lua source to a stripped chunk, as tools/luac_psx does on the PS1. */
#define LUA_CORE

#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lundump.h"
#include "psxlua_compile.h"

static void *alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

typedef struct {
  const char *s;
  size_t size;
} Source;

static const char *reader(lua_State *L, void *ud, size_t *size) {
  Source *src = (Source *)ud;
  (void)L;
  if (src->size == 0) return NULL;
  *size = src->size;
  src->size = 0;
  return src->s;
}

typedef struct {
  psxlua_writer writer;
  void *ud;
} Sink;

static int sink_write(lua_State *L, const void *p, size_t size, void *ud) {
  Sink *sink = (Sink *)ud;
  (void)L;
  return sink->writer(p, size, sink->ud);
}

int psxlua_compile(const char *source, size_t len, const char *chunkname, psxlua_writer writer, void *ud,
                   char *err, size_t errsize) {
  Source src;
  Sink sink;
  lua_State *L = lua_newstate(alloc, NULL);
  int status;
  if (!L) {
    strncpy(err, "cannot create Lua state", errsize - 1);
    err[errsize - 1] = 0;
    return 1;
  }
  src.s = source;
  src.size = len;
  /* mode NULL, as luaL_loadbuffer in luac_psx */
  status = lua_load(L, reader, &src, chunkname, NULL);
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    strncpy(err, msg ? msg : "compilation failed", errsize - 1);
    err[errsize - 1] = 0;
    lua_close(L);
    return 1;
  }
  sink.writer = writer;
  sink.ud = ud;
  status = luaU_dump(L, getproto(L->top - 1), sink_write, &sink, 1);
  lua_close(L);
  if (status != 0) {
    strncpy(err, "bytecode dump failed", errsize - 1);
    err[errsize - 1] = 0;
    return 1;
  }
  return 0;
}
