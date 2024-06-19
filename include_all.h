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
    #include <ImNodes.h>
    #include <ImNodesEz.h>
#endif

#include "include/glad/glad.h"
#include "include/glm/glm/glm.hpp"
#include <btBulletDynamicsCommon.h>
#include <iostream>
#include <fstream>
#include <thread>

#ifdef _WIN32
    #include "pybind11/embed.h"
    #include "pybind11/pybind11.h"
    #include "pybind11/stl.h"
#else
    #include <python3.11/Python.h>

    #include <pybind11/embed.h>
    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>
#endif

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavutil/pixfmt.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
}

#ifndef GAME_SHIPPING
    #include <nlohmann/json.hpp>
#else
    #include "include/nlohmann/include/nlohmann/json.hpp"
#endif

/* NameSpaces */
namespace fs = std::filesystem;
namespace py = pybind11;

using namespace py::literals;
using json = nlohmann::json;

#include "Engine/Core/LoD.cpp"
#include "Engine/Scripting/math.cpp"
#include "Engine/Physics/PhysicsManager.cpp"
#include "Engine/GUI/Text/Text.h"
#include "Engine/GUI/Button/Button.h"
#include "Engine/GUI/Video/video.cpp"
#include "Engine/Core/Engine.hpp"
#include "Engine/Lighting/shaders/shaders.h"
#include "Engine/Lighting/lights.h"
#include "Engine/Scripting/time.cpp"
#include "Engine/Scripting/functions.cpp"
#include "Engine/Core/functions.h"
#include "Engine/Core/global_variables.cpp"
#include "Engine/Editor/SceneEditor/SceneEditor.h"
#include "Engine/GUI/Tooltip/Tooltip.cpp"
#include "Engine/GUI/Text/Text.cpp"
#include "Engine/GUI/Button/Button.cpp"
#include "Engine/Core/Entity.hpp"
#include "Engine/Core/Entity.cpp"
#include "Engine/Core/Engine.cpp"

#ifndef GAME_SHIPPING
    #include "Engine/Core/Core.h"
    #include "Engine/Editor/SceneEditor/SceneEditor.h"
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.h"
    #include "Engine/Editor/UiScripts/UiScripts.cpp"
    #include "Engine/Editor/Styles/Styles.h"
    #include "Engine/Editor/Styles/Styles.cpp"
#endif

#include "Engine/Lighting/InitLighting.cpp"
#include "Engine/Lighting/skybox.cpp"
#include "Engine/Core/SaveLoad.cpp"

#ifndef GAME_SHIPPING
    #include "Engine/Editor/CodeEditor/CodeEditor.h"
    #include "Engine/Editor/CodeEditor/CodeEditor.cpp"
    #include "Engine/Editor/SceneEditor/SceneEditor.h"
    #include "Engine/Editor/SceneEditor/Gizmo/Gizmo.h"
    #include "Engine/Editor/SceneEditor/Gizmo/Gizmo.cpp"
    #include "Engine/Editor/SceneEditor/SceneEditor.cpp"
    #include "Engine/Editor/EntitiesList/EntitiesList.cpp"
    #include "Engine/Editor/MaterialsNodeEditor/MaterialsNodeEditor.h"
    #include "Engine/Editor/MaterialsNodeEditor/Nodes.cpp"
    #include "Engine/Editor/MaterialsNodeEditor/MaterialsNodeEditor.cpp"
    #include "Engine/Editor/Inspector/Inspector.h"
    #include "Engine/Editor/Inspector/Inspector.cpp"
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.h"
    #include "Engine/Editor/AssetsExplorer/file_manipulation.h"
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.cpp"
    #include "Engine/Core/Core.cpp"
    #include "Engine/Editor/MenuBar/MenuBar.cpp"

    #include "GameBuilder/builder.cpp"
#endif

#include "Engine/Core/RunGame.cpp"

#endif // INCLUDE_ALL_H