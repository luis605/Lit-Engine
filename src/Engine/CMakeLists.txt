cmake_minimum_required(VERSION 3.28)
add_library(Engine STATIC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v0.21
)
FetchContent_MakeAvailable(meshoptimizer)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b-docking
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
FetchContent_GetProperties(imguizmo)
if(NOT imguizmo_POPULATED)
    FetchContent_Populate(imguizmo)
endif()
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

add_library(lit_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(lit_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${imguizmo_SOURCE_DIR}/src
)
target_compile_definitions(lit_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(lit_imgui PUBLIC glfw)
set_target_properties(lit_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(lit_stb INTERFACE)
target_include_directories(lit_stb INTERFACE ${stb_SOURCE_DIR})

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
        Render/Material.cppm
        Render/RenderSettings.cppm
        Render/Lighting.cppm
        Render/CustomPasses.cppm
        World/World.cppm
        Portal/PortalView.cppm
        Portal/Portal.cppm
        Events/Events.cppm
        Physics/Physics.cppm
        History/History.cppm
        Animation/Animation.cppm
        Jobs/Jobs.cppm
        Profiler/Profiler.cppm
        Text/LineEditor.cppm
        Math/GizmoMath.cppm
        Selection/Selection.cppm
        Input/Input.cppm
        Input/InputActions.cppm
        Asset/AssetManager.cppm
        UI/Manager.cppm
        UI/Text.cppm
        UI/ImGuiLayer.cppm
        Tools/Rng.cppm
        Tools/Json.cppm
        Tools/Replay.cppm
        Tools/Dump.cppm
        Tools/Perf.cppm
        Params/Params.cppm
        Observation/Observation.cppm
        State/State.cppm
        Storylet/Storylet.cppm
        Save/Save.cppm
        Camera/CameraRig.cppm
        Geometry/Geometry.cppm
        AI/AI.cppm
        Nav/Nav.cppm
        Animation/Rig.cppm
        Animation/Face.cppm
        Audio/Propagation.cppm
        Audio/Audio.cppm
    PRIVATE
        Engine.cpp
        Asset/AssetManager.cpp
        Render/Renderer.cpp
        Render/Camera.cpp
        Input/Input.cpp
        Log/Log.cpp
        UI/Manager.cpp
        UI/Text.cpp
        UI/ImGuiLayer.cpp
        Render/Mesh.cpp
        Render/Lighting.cpp
        Render/CustomPasses.cpp
        World/World.cpp
        Portal/Portal.cpp
        Portal/PortalScene.cpp
        Physics/Physics.cpp
        History/History.cpp
        Animation/Animation.cpp
        Jobs/Jobs.cpp
        Profiler/Profiler.cpp
        Text/LineEditor.cpp
        Math/GizmoMath.cpp
        Tools/Json.cpp
        Tools/Replay.cpp
        Tools/Dump.cpp
        Tools/Perf.cpp
        Params/Params.cpp
        Observation/Observation.cpp
        State/State.cpp
        Storylet/Storylet.cpp
        Save/Save.cpp
        Camera/CameraRig.cpp
        Geometry/Geometry.cpp
        AI/AI.cpp
        Nav/Nav.cpp
        Animation/Rig.cpp
        Animation/Face.cpp
        Audio/Propagation.cpp
        Audio/Audio.cpp
)

target_compile_features(Engine PUBLIC cxx_std_23)

if(LIT_SANITIZE)
    target_compile_options(Engine PUBLIC -fsanitize=${LIT_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(Engine PUBLIC -fsanitize=${LIT_SANITIZE})
    if(LIT_SANITIZE MATCHES "undefined" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        get_target_property(moduleInterfaces Engine CXX_MODULE_SET_cxx_modules)
        set_source_files_properties(${moduleInterfaces} PROPERTIES COMPILE_OPTIONS -fno-sanitize=undefined)
    endif()
endif()

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

target_link_libraries(Engine PRIVATE glfw assimp freetype meshoptimizer lit_stb)
target_link_libraries(Engine PUBLIC lit_imgui)
target_link_libraries(Engine PUBLIC
    Diligent-GraphicsEngineVk-static
    $<$<BOOL:${WIN32}>:Diligent-GraphicsEngineD3D11-static>
    $<$<BOOL:${WIN32}>:Diligent-GraphicsEngineD3D12-static>
)