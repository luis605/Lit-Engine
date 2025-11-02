cmake_minimum_required(VERSION 3.28)

project(LitEditor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend" FORCE)
set(GLFW_BUILD_X11   ON  CACHE BOOL "Build X11 backend" FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -g")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable Assimp unit tests")
seT(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Disable Assimp tools")
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Disable Assimp samples")
set(ASSIMP_NO_EXPORT ON CACHE BOOL "Disable Assimp export functionality")
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "Disable Assimp viewer")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries")
set(ASSIMP_INSTALL OFF CACHE BOOL "Disable Assimp install target")
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "Disable all exporters")

add_subdirectory(src/vendors/assimp)
add_subdirectory(src/vendors/glfw)
add_subdirectory(src/vendors/freetype)
add_subdirectory(src/Engine)
add_subdirectory(src/Editor)

add_custom_target(run
    DEPENDS Editor
    COMMAND ${CMAKE_COMMAND} -E echo "Running Editor..."
    COMMAND $<TARGET_FILE:Editor>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Builds and runs the Editor executable"
)