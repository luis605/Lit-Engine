cmake_minimum_required(VERSION 3.28)
project(LitEditor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend" FORCE)
set(GLFW_BUILD_X11   ON  CACHE BOOL "Build X11 backend" FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmodules-ts -fdiagnostics-color=always -g")

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