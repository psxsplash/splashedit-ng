#include "ui/brand.h"

#include <SDL3/SDL.h>
#include <stb_image.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "gl.h"
#include "ui/theme.h"

using namespace gl;

namespace brand {

namespace {
GLuint g_logo = 0;
ImVec2 g_uv0, g_uv1;
float g_aspect = 1;
ImU32 g_bg = 0;
}  // namespace

bool setWindowIcon(SDL_Window* window, const char* assetDir) {
    std::string file = std::string(assetDir) + "/brand/icon-64.png";
    int w, h, n;
    unsigned char* px = stbi_load(file.c_str(), &w, &h, &n, 4);
    if (!px) {
        std::fprintf(stderr, "window icon: can't load %s\n", file.c_str());
        return false;
    }
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    bool ok = s && SDL_SetWindowIcon(window, s);
    if (!ok) std::fprintf(stderr, "window icon: %s\n", SDL_GetError());
    SDL_DestroySurface(s);
    stbi_image_free(px);
    return ok;
}

bool load(const char* assetDir) {
    std::string file = std::string(assetDir) + "/brand/logo-dark.png";
    int w, h, n;
    unsigned char* px = stbi_load(file.c_str(), &w, &h, &n, 4);
    if (!px) {
        std::fprintf(stderr, "logo: can't load %s\n", file.c_str());
        return false;
    }
    // The PNG is the wordmark centred on a wide field of its background
    // colour. Crop to the pixels that differ from the corner, plus a margin.
    const unsigned char* bg = px;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char* p = px + (y * w + x) * 4;
            int d = std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]);
            if (d > 30) {
                x0 = std::min(x0, x), x1 = std::max(x1, x);
                y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < 0) x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
    int margin = (y1 - y0) / 12;
    x0 = std::max(0, x0 - margin), y0 = std::max(0, y0 - margin);
    x1 = std::min(w - 1, x1 + margin), y1 = std::min(h - 1, y1 + margin);
    g_uv0 = ImVec2((float)x0 / w, (float)y0 / h);
    g_uv1 = ImVec2((float)(x1 + 1) / w, (float)(y1 + 1) / h);
    g_aspect = (float)(x1 + 1 - x0) / (float)(y1 + 1 - y0);
    g_bg = IM_COL32(bg[0], bg[1], bg[2], 255);

    glGenTextures(1, &g_logo);
    glBindTexture(GL_TEXTURE_2D, g_logo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);
    return true;
}

float drawLogo(ImDrawList* dl, ImVec2 pos, float height) {
    if (!g_logo) return 0;
    ImVec2 max(pos.x + height * g_aspect, pos.y + height);
    dl->AddRectFilled(pos, max, g_bg, theme::radius::button);
    dl->AddImageRounded((ImTextureID)(intptr_t)g_logo, pos, max, g_uv0, g_uv1, IM_COL32_WHITE, theme::radius::button);
    return max.x - pos.x;
}

}  // namespace brand
