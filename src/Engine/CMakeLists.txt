cmake_minimum_required(VERSION 3.28)
add_library(Engine STATIC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_sources(Engine
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
    FILES
        Engine.cppm
        GLM.cppm
        Render/Renderer.cppm
        Render/Camera.cppm
        Render/Component.cppm
        Render/Entity.cppm
        Render/SceneDatabase.cppm
        Render/Mesh.cppm
        Input/Input.cppm
        Asset/AssetManager.cppm
        UI/Manager.cppm
        UI/Text.cppm
    PRIVATE
        Engine.cpp
        Asset/AssetManager.cpp
        Render/Renderer.cpp
        Render/Camera.cpp
        Input/Input.cpp
        Log/Log.cpp
        UI/Manager.cpp
        UI/Text.cpp

)

target_compile_features(Engine PUBLIC cxx_std_23)

target_include_directories(Engine PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/assimp/include"

    "${PROJECT_SOURCE_DIR}/src/vendors/glm"
    "${PROJECT_SOURCE_DIR}/src/vendors/freetype/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/DiligentEngine"
    "${PROJECT_SOURCE_DIR}/src"
)

if(WIN32)
    target_compile_definitions(Engine PUBLIC PLATFORM_WIN32=1)
elseif(APPLE)
    target_compile_definitions(Engine PUBLIC PLATFORM_MACOS=1)
elseif(UNIX)
    target_compile_definitions(Engine PUBLIC PLATFORM_LINUX=1)
endif()

target_link_libraries(Engine PRIVATE glfw assimp freetype)
target_link_libraries(Engine PUBLIC
    Diligent-GraphicsEngineOpenGL-static
    Diligent-GraphicsEngineVk-static
    $<$<BOOL:${WIN32}>:Diligent-GraphicsEngineD3D11-static>
    $<$<BOOL:${WIN32}>:Diligent-GraphicsEngineD3D12-static>
)