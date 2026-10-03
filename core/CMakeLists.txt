set(RECAST_DIR ${recastnavigation_SOURCE_DIR}/Recast)
add_library(recast STATIC
    ${RECAST_DIR}/Source/Recast.cpp
    ${RECAST_DIR}/Source/RecastAlloc.cpp
    ${RECAST_DIR}/Source/RecastArea.cpp
    ${RECAST_DIR}/Source/RecastAssert.cpp
    ${RECAST_DIR}/Source/RecastContour.cpp
    ${RECAST_DIR}/Source/RecastFilter.cpp
    ${RECAST_DIR}/Source/RecastLayers.cpp
    ${RECAST_DIR}/Source/RecastMesh.cpp
    ${RECAST_DIR}/Source/RecastMeshDetail.cpp
    ${RECAST_DIR}/Source/RecastRasterization.cpp
    ${RECAST_DIR}/Source/RecastRegion.cpp)
target_include_directories(recast SYSTEM PUBLIC ${RECAST_DIR}/Include)
# Same float rules as splashcore: the nav mesh has to match DotRecast's.
if(MSVC)
    target_compile_options(recast PRIVATE /fp:precise)
else()
    target_compile_options(recast PRIVATE -ffp-contract=off)
endif()

add_library(splashcore STATIC
    scene.cpp
    texture.cpp
    vrampacker.cpp
    bvh.cpp
    navregion.cpp
    splashpack.cpp
    stb_impl.cpp)
target_include_directories(splashcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(splashcore SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(splashcore PUBLIC nlohmann_json::nlohmann_json PRIVATE recast)
# The writer reproduces SplashEdit 2.4's float results bit for bit: no FMA
# contraction, no fast-math.
if(MSVC)
    target_compile_options(splashcore PRIVATE /W4 /fp:precise)
else()
    target_compile_options(splashcore PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
