/*
** psxlua's ldump.c, compiled so that the chunk is laid out as on the PS1:
** size_t is 4 bytes there, both in the header and in every string length.
** int, Instruction and lua_Number are already 4 bytes on the host, and all
** supported hosts are little-endian like the PS1 (checked in luacompile.cpp).
*/
#define ldump_c
#define LUA_CORE

#include <stddef.h>

#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

static void psx_luaU_header(lu_byte *h) {
  luaU_header(h);
  h[8] = 4; /* sizeof(size_t) on the PS1 */
}

#define luaU_header psx_luaU_header
#define size_t lu_int32
#include "ldump.c"
