cmake_minimum_required(VERSION 3.28)
add_library(Engine STATIC)

set_target_properties(Engine PROPERTIES
    CXX_MODULE_STD ON
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_sources(Engine
    PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES
    FILES
        Engine.cppm
        GLM.cppm
        Render/Renderer.cppm
        Render/Camera.cppm
        Render/Shader.cppm
        Render/Component.cppm
        Render/Entity.cppm
        Render/SceneDatabase.cppm
        Render/ShaderManager.cppm
        Render/Mesh.cppm
        Input/Input.cppm
        Asset/AssetManager.cppm
    PRIVATE
        Engine.cpp
        Asset/AssetManager.cpp
        Render/Renderer.cpp
        Render/Camera.cpp
        Render/ShaderManager.cpp
        Render/Shader.cpp
        Input/Input.cpp
        ${PROJECT_SOURCE_DIR}/src/vendors/glad/src/glad.c
)

target_compile_features(Engine PUBLIC cxx_std_23)

target_include_directories(Engine PRIVATE
    "${PROJECT_SOURCE_DIR}/src/vendors/glfw/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/assimp/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/glad/include"
    "${PROJECT_SOURCE_DIR}/src/vendors/glm"
)

target_link_libraries(Engine PRIVATE glfw assimp)