cmake_minimum_required(VERSION 3.28)
project(LitEditor LANGUAGES CXX)

set(GLFW_BUILD_WAYLAND ON CACHE BOOL "Build Wayland backend" FORCE)
set(GLFW_BUILD_X11   OFF CACHE BOOL "Disable X11 backend" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(Editor)

target_sources(Editor
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Application.cppm
    PRIVATE
        Application/Application.cpp
        main.cpp
        ${PROJECT_SOURCE_DIR}/../vendors/glad/src/glad.c
)

target_include_directories(Editor PRIVATE
    "${PROJECT_SOURCE_DIR}/../vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/../vendors/glad/include"
)

target_link_libraries(Editor PRIVATE Engine glfw)
