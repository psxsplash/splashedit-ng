add_library(splashcore STATIC
    scene.cpp
    texture.cpp
    vrampacker.cpp
    bvh.cpp
    splashpack.cpp
    stb_impl.cpp)
target_include_directories(splashcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(splashcore SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(splashcore PUBLIC nlohmann_json::nlohmann_json)
# The writer reproduces SplashEdit 2.4's float results bit for bit: no FMA
# contraction, no fast-math.
if(MSVC)
    target_compile_options(splashcore PRIVATE /W4 /fp:precise)
else()
    target_compile_options(splashcore PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
