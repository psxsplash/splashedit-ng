/*
** Host configuration for psxlua's compiler. Force-included before every
** psxlua source file (see core/CMakeLists.txt).
**
** psxlua's luaconf.h makes lua_Number a C `long`, which is 32 bits on the
** PS1 but 64 bits on most hosts. This header includes that luaconf.h, then
** replaces the number type and the number operations so that the compiler
** (lexer, parser, constant folding) computes exactly what the PS1 does:
** 32-bit two's complement with wraparound, C truncating division, and the
** MIPS results for INT_MIN / -1 (INT_MIN) and INT_MIN % -1 (0).
**
** Because this is included before each .c file defines LUA_CORE, lobject_c
** etc., the sections of luaconf.h guarded by those macros are skipped; every
** macro they would define is provided below.
*/
#ifndef psxlua_host_h
#define psxlua_host_h

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
#define __builtin_unreachable() __assume(0)
#define __builtin_memcpy memcpy
#if !defined(__cplusplus)
#define inline __inline
#endif
#endif

#include <luaconf.h>

#undef LUA_NUMBER
#define LUA_NUMBER int32_t
#undef LUAI_UACNUMBER
#define LUAI_UACNUMBER int32_t

/*
** psxlua parses numerals with luaA_strtol (llibc.c). For a decimal numeral
** with a leading zero other than "0" itself ("010", "0.5") it returns
** without setting endptr, and the PS1 build then reports "malformed number".
** Starting endptr at s gives that same error deterministically.
*/
static inline int32_t psxlua_str2number(const char *s, char **p, int base) {
  *p = (char *)s;
  return (int32_t)luaA_strtol(s, p, base);
}
#undef lua_str2number
#define lua_str2number(s, p) psxlua_str2number((s), (p), 10)
#undef lua_strx2number
#define lua_strx2number(s, p) psxlua_str2number((s), (p), 16)

#define PSXLUA_I32(x) ((int32_t)(uint32_t)(x))

static inline int32_t psxlua_div(int32_t a, int32_t b) {
  if (b == 0) return 0; /* never folded by lcode.c; the PS1 would trap */
  if (b == -1) return PSXLUA_I32(0u - (uint32_t)a);
  return a / b;
}

static inline int32_t psxlua_mod(int32_t a, int32_t b) {
  if (b == 0 || b == -1) return 0;
  return a % b;
}

/*
** psxlua's luai_numpowimpl: r = a, then r *= a once per iteration of
** `for (unsigned i = 0; i < b; i++)`. So a^b yields a^(b+1), and b is
** compared as unsigned. Same result, without the loop.
*/
static inline int32_t psxlua_pow(int32_t a, int32_t b) {
  uint32_t base = (uint32_t)a, n = (uint32_t)b, r = (uint32_t)a;
  while (n) {
    if (n & 1u) r *= base;
    base *= base;
    n >>= 1;
  }
  return PSXLUA_I32(r);
}

#undef luai_numadd
#undef luai_numsub
#undef luai_nummul
#undef luai_numdiv
#undef luai_nummod
#undef luai_numpow
#undef luai_numunm
#undef luai_numeq
#undef luai_numlt
#undef luai_numle
#undef luai_numisnan
#define luai_numadd(L, a, b) PSXLUA_I32((uint32_t)(a) + (uint32_t)(b))
#define luai_numsub(L, a, b) PSXLUA_I32((uint32_t)(a) - (uint32_t)(b))
#define luai_nummul(L, a, b) PSXLUA_I32((uint32_t)(a) * (uint32_t)(b))
#define luai_numdiv(L, a, b) psxlua_div((a), (b))
#define luai_nummod(L, a, b) psxlua_mod((a), (b))
#define luai_numpow(L, a, b) psxlua_pow((a), (b))
#define luai_numunm(L, a) PSXLUA_I32(0u - (uint32_t)(a))
#define luai_numeq(a, b) ((a) == (b))
#define luai_numlt(L, a, b) ((a) < (b))
#define luai_numle(L, a, b) ((a) <= (b))
#define luai_numisnan(L, a) 0

#endif
