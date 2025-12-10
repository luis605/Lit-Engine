cmake_minimum_required(VERSION 3.28)

project(LitEditor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Disable Wayland backend" FORCE)
set(GLFW_BUILD_X11   ON  CACHE BOOL "Build X11 backend" FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -g")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "Disable Assimp unit tests")
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "Disable Assimp tools")
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "Disable Assimp samples")
set(ASSIMP_NO_EXPORT ON CACHE BOOL "Disable Assimp export functionality")
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "Disable Assimp viewer")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries")
set(ASSIMP_INSTALL OFF CACHE BOOL "Disable Assimp install target")
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "Disable all exporters")

add_subdirectory(src/vendors/assimp)
add_subdirectory(src/vendors/glfw)
add_subdirectory(src/vendors/freetype)

set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "Disable Diligent Samples" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "Disable Diligent Tests" FORCE)
set(DILIGENT_BUILD_TOOLS OFF CACHE BOOL "Disable Diligent Tools" FORCE)
set(DILIGENT_INSTALL_CORE OFF CACHE BOOL "Don't install Diligent" FORCE)
set(DILIGENT_BUILD_GRAPHICS_ENGINE_OPENGL ON CACHE BOOL "Build GL Backend" FORCE)
set(DILIGENT_BUILD_GRAPHICS_ENGINE_VULKAN ON CACHE BOOL "Build Vulkan Backend" FORCE)

if(WIN32)
    set(DILIGENT_BUILD_GRAPHICS_ENGINE_D3D11 ON CACHE BOOL "Build D3D11 Backend" FORCE)
    set(DILIGENT_BUILD_GRAPHICS_ENGINE_D3D12 ON CACHE BOOL "Build D3D12 Backend" FORCE)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vendors/DiligentCore")
    add_subdirectory(src/vendors/DiligentCore)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vendors/DiligentEngine")
    add_subdirectory(src/vendors/DiligentEngine)
else()
    message(FATAL_ERROR "Diligent source not found in vendors. Expected DiligentCore or DiligentEngine.")
endif()

add_subdirectory(src/Engine)
add_subdirectory(src/Editor)

add_custom_target(run
    DEPENDS Editor
    COMMAND ${CMAKE_COMMAND} -E echo "Running Editor..."
    COMMAND ${CMAKE_COMMAND} -E env $<TARGET_FILE:Editor>
    USES_TERMINAL
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Builds and runs the Editor executable"
)