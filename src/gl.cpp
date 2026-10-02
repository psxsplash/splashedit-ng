#include "gl.h"

#include <SDL3/SDL.h>

namespace gl {
#define SPLASH_GL_DEFINE(type, name) type name = nullptr;
SPLASH_GL_FUNCS(SPLASH_GL_DEFINE)
#undef SPLASH_GL_DEFINE

bool load() {
    bool ok = true;
#define SPLASH_GL_LOAD(type, name)                                                       \
    {                                                                                    \
        const char* sym = #name;                                                         \
        char buf[64];                                                                    \
        SDL_strlcpy(buf, sym, sizeof(buf));                                              \
        size_t len = SDL_strlen(buf);                                                    \
        if (len && buf[len - 1] == '_') buf[len - 1] = 0;                                \
        name = reinterpret_cast<type>(SDL_GL_GetProcAddress(buf));                       \
        if (!name) {                                                                     \
            SDL_Log("missing GL entry point %s", buf);                                   \
            ok = false;                                                                  \
        }                                                                                \
    }
    SPLASH_GL_FUNCS(SPLASH_GL_LOAD)
#undef SPLASH_GL_LOAD
    return ok;
}
}  // namespace gl
