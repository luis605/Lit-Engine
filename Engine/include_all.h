// TODO: Include only headers in here (CMAKE already supports this change)

#ifndef INCLUDE_ALL_H_
#define INCLUDE_ALL_H_

#include <extras/IconsFontAwesome6.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include <raylib.h>
#include <raymath.h>
#include <rcamera.h>
#include <rlgl.h>
#include <custom.h>
#include <rlFrustum.cpp>
#include <meshoptimizer.h>

#ifndef GAME_SHIPPING
    #include <imgui/imgui.h>
    #include <imgui/imgui_internal.h>
    #include <rlImGui.h>
    #include <TextEditor.h>
    #include "../include/NodeEditor/imgui_node_editor.h"
    #include "../include/NodeEditor/imgui_canvas.h"
    #include "../include/NodeEditor/examples/application/include/application.h"

    #include <future>
#endif

#include "../include/glad/glad.h"
#include "../include/glm/glm/glm.hpp"
#include <btBulletDynamicsCommon.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <any>
#include <queue>

#ifdef _WIN32
    #include "pybind11/embed.h"
    #include "pybind11/pybind11.h"
    #include "pybind11/functional.h"
    #include "pybind11/stl.h"
#else
    #include <python3.11/Python.h>

    #include <pybind11/embed.h>
    #include <pybind11/pybind11.h>
    #include <pybind11/functional.h>
    #include <pybind11/stl.h>
#endif

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
}


#ifndef GAME_SHIPPING
    #include <nlohmann/json.hpp>
#else
    #include "../include/nlohmann/include/nlohmann/json.hpp"
#endif

/* NameSpaces */
namespace fs = std::filesystem;
namespace py = pybind11;

using namespace py::literals;
using json = nlohmann::json;

#include "Core/Events.h"
#include "Editor/MenuBar/Settings.h"
#include "Core/LoD.cpp"
#include "Scripting/math.cpp"
#include "Physics/PhysicsManager.cpp"
#include "GUI/Text/Text.h"
#include "GUI/Button/Button.h"
#include "GUI/Video/video.cpp"
#include "Core/Engine.hpp"
#include "Lighting/shaders/shaders.h"
#include "Lighting/lights.h"
#include "Scripting/time.cpp"
#include "Scripting/functions.cpp"
#include "Core/functions.h"
#include "Core/global_variables.cpp"
#include "Editor/SceneEditor/SceneEditor.h"
#include "GUI/Tooltip/Tooltip.cpp"
#include "GUI/Text/Text.cpp"
#include "GUI/Button/Button.cpp"

#ifndef GAME_SHIPPING
    #include "Plugins/Scripting.cpp"
    #include "Plugins/Loader.cpp"
#endif

#include "Core/Entity.hpp"
#include "Core/Entity.cpp"
#include "Core/Engine.cpp"

#ifndef GAME_SHIPPING
    #include "Core/Core.h"
    #include "Editor/SceneEditor/SceneEditor.h"
    #include "Editor/AssetsExplorer/AssetsExplorer.h"
    #include "Editor/UiScripts/UiScripts.cpp"
    #include "Editor/Styles/Styles.h"
    #include "Editor/Styles/Styles.cpp"
    #include "Editor/Styles/ImGuiExtras.cpp"
#endif

#include "Lighting/InitLighting.cpp"
#include "Lighting/skybox.cpp"
#include "Core/SaveLoad.cpp"

#ifndef GAME_SHIPPING
    #include "Editor/CodeEditor/CodeEditor.h"
    #include "Editor/CodeEditor/CodeEditor.cpp"
    #include "Editor/SceneEditor/SceneEditor.h"
    #include "Editor/SceneEditor/Gizmo/Gizmo.h"
    #include "Editor/SceneEditor/Gizmo/Gizmo.cpp"
    #include "Editor/SceneEditor/SceneEditor.cpp"
    #include "Editor/EntitiesList/EntitiesList.cpp"
    #include "Editor/MaterialsNodeEditor/MaterialNodeEditor.cpp"
    #include "Editor/Inspector/Inspector.h"
    #include "Editor/Inspector/Inspector.cpp"
    #include "Editor/AssetsExplorer/AssetsExplorer.h"
    #include "Editor/AssetsExplorer/file_manipulation.h"
    #include "Editor/AssetsExplorer/AssetsExplorer.cpp"
    #include "Core/Core.cpp"
    #include "Editor/MenuBar/MenuBar.cpp"

    #include "../GameBuilder/builder.cpp"
#endif

#include "Core/RunGame.cpp"

#endif // INCLUDE_ALL_H