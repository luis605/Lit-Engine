cmake_minimum_required(VERSION 3.28)
add_library(Engine STATIC)

set(GLFW_BUILD_WAYLAND ON CACHE BOOL "Build Wayland backend" FORCE)
set(GLFW_BUILD_X11   OFF CACHE BOOL "Disable X11 backend" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_sources(Engine
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
    FILES
        Engine.cppm
        Renderer.cppm
        Camera.cppm
    PRIVATE
        Engine.cpp
        Renderer.cpp
        Camera.cpp
        ${PROJECT_SOURCE_DIR}/src/vendors/glad/src/glad.c
)

target_compile_features(Engine PUBLIC cxx_std_23)

target_include_directories(Engine PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/glad/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/glm"
)

target_link_libraries(Engine PRIVATE glfw)