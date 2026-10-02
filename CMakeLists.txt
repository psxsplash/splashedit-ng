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

find_package(SDL3 REQUIRED CONFIG)
find_package(Freetype REQUIRED)
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
if(MSVC)
    target_compile_options(splashedit PRIVATE /W4)
else()
    target_compile_options(splashedit PRIVATE -Wall -Wextra)
endif()
