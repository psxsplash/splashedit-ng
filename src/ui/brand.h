#pragma once

#include <SDL3/SDL_video.h>
#include <imgui.h>

// psxsplash branding from assets/brand: the window icon and the logo-dark
// wordmark. Nothing here draws a logo of its own.
namespace brand {

// Sets the window icon from brand/icon-64.png. Returns false if it can't be loaded.
bool setWindowIcon(SDL_Window* window, const char* assetDir);

// Uploads brand/logo-dark.png, cropped to the wordmark plus a margin of its
// own background. Needs a current GL context.
bool load(const char* assetDir);

// Draws the wordmark `height` pixels tall with its top-left at `pos`, on a
// rounded badge of the logo's background. Returns the width drawn, 0 if the
// logo isn't loaded.
float drawLogo(ImDrawList* dl, ImVec2 pos, float height);

}  // namespace brand
