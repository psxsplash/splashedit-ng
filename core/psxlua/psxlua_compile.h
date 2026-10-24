#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called with consecutive pieces of the chunk; return nonzero to abort. */
typedef int (*psxlua_writer)(const void *p, size_t size, void *ud);

/* Compiles Lua source and writes the chunk, without line numbers and local
** names when strip is nonzero (as tools/luac_psx does). Returns 0 on success;
** otherwise writes the Lua error message to err. */
int psxlua_compile(const char *source, size_t len, const char *chunkname, int strip, psxlua_writer writer, void *ud,
                   char *err, size_t errsize);

#ifdef __cplusplus
}
#endif
