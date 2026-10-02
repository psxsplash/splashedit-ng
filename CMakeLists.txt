cmake_minimum_required(VERSION 3.24)
project(splashedit-ng LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(imgui stb)

# Use system SDL3 and FreeType when present, otherwise build them, so a
# plain Windows checkout configures without a package manager.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()
find_package(Freetype QUIET)
if(NOT FREETYPE_FOUND)
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-14-3
        GIT_SHALLOW TRUE)
    foreach(dep BZIP2 PNG ZLIB BROTLI HARFBUZZ)
        set(FT_DISABLE_${dep} ON CACHE BOOL "" FORCE)
    endforeach()
    FetchContent_MakeAvailable(freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Freetype::Freetype OpenGL::GL)

add_executable(splashedit
    src/main.cpp
    src/gl.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    src/viewport/ps1view.cpp
    src/mockup/main_screen.cpp)
target_include_directories(splashedit PRIVATE src)
target_include_directories(splashedit SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(splashedit PRIVATE imgui)
target_compile_definitions(splashedit PRIVATE SPLASHEDIT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(WIN32)
    target_compile_definitions(splashedit PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(splashedit PRIVATE /W4 /utf-8)
else()
    target_compile_options(splashedit PRIVATE -Wall -Wextra)
endif()
