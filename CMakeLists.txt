cmake_minimum_required(VERSION 3.28)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444" CACHE INTERNAL "Enable experimental import std")
project(LitEditor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend" FORCE)
set(GLFW_BUILD_X11   ON  CACHE BOOL "Build X11 backend" FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmodules -fdiagnostics-color=always -g")

add_subdirectory(src/vendors/glfw)
add_subdirectory(src/Engine)
add_subdirectory(src/Editor)

add_custom_target(run
    DEPENDS Editor
    COMMAND ${CMAKE_COMMAND} -E echo "Running Editor..."
    COMMAND $<TARGET_FILE:Editor>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Builds and runs the Editor executable"
)