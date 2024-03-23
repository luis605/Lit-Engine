#ifndef INCLUDE_ALL_H_
#define INCLUDE_ALL_H_

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif


#include "include/raylib/src/raylib.h"
#include "include/raylib/src/raymath.h"
#include "include/raylib/src/rcamera.h"
#include "include/raylib/src/rlgl.h"
#include "include/custom.h"
#include "include/rlFrustum.cpp"

#include "meshoptimizer/src/meshoptimizer.h" 


#ifndef GAME_SHIPPING
    #include "imgui/imgui.h"
    #include "imgui/imgui_internal.h"
    #include "include/rlImGui.h"
    #include "include/ImGuiColorTextEdit/TextEditor.h"

    #include "include/ImNodes/ImNodes.h"
    #include "include/ImNodes/ImNodesEz.h"
#endif

#include "include/glad/glad.h"

#define SUPPORT_FILEFORMAT_HDR      1


#include "include/bullet3/src/BulletCollision/BroadphaseCollision/btBroadphaseProxy.h"

#include "include/bullet3/src/BulletCollision/CollisionShapes/btCollisionShape.h"
#include "include/bullet3/src/BulletCollision/CollisionShapes/btConvexPolyhedron.h"
#include "include/bullet3/src/BulletCollision/CollisionShapes/btShapeHull.h"

#include "include/bullet3/src/LinearMath/btVector3.h"
#include "include/bullet3/src/btBulletDynamicsCommon.h"

#ifdef _WIN32
    extern "C" {
        #include <libavcodec/avcodec.h>
        #include <libavformat/avformat.h>
        #include <libswscale/swscale.h>
    }
#else
    extern "C" {
        #include "include/ffmpeg/libavcodec/avcodec.h"
        #include "include/ffmpeg/libavformat/avformat.h"
        #include "include/ffmpeg/libswscale/swscale.h"
}
#endif

#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <exception>
#include <signal.h>
#include "include/glm/glm.hpp"
#include "include/glm/gtc/matrix_transform.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

#include <limits>
#include <omp.h>

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

#include <sstream>
#include <regex>

#ifndef GAME_SHIPPING
    #include <nlohmann/json.hpp>
#else
    #include "include/nlohmann/include/nlohmann/json.hpp"
#endif

#ifndef GAME_SHIPPING
    #include <IconsFontAwesome.h>
#endif

#include <typeinfo>
#include <thread>
#include <functional>
#include <future>
#include <iomanip>
#include <unordered_map>
#include <map>

/* Physx */


/* NameSpaces */
namespace fs = std::filesystem;
namespace py = pybind11;
using namespace py::literals;

using json = nlohmann::json;


// Critical
#include "globals.h"
#include "Engine/Core/LoD.cpp"
#include "Engine/Scripting/math.cpp"

// Physics
#include "Engine/Physics/PhysicsManager.cpp"

/* Game Objects */
#include "Engine/GUI/Text/Text.h"
#include "Engine/GUI/Button/Button.h"
#include "Engine/GUI/Video/video.cpp"
#include "Engine/Core/Engine.hpp"
#include "Engine/Lighting/lights.h"

/* Scripting */
#include "Engine/Scripting/time.cpp"
#include "Engine/Scripting/functions.cpp"


/* Globals */
#include "Engine/Core/functions.h"
#include "Engine/Core/global_variables.cpp"
#include "Engine/Editor/SceneEditor/SceneEditor.h"

#include "Engine/Core/Engine.cpp"
#include "Engine/Core/RunGame.h"


/* GUI */
#include "Engine/GUI/Tooltip/Tooltip.cpp"
#include "Engine/GUI/Text/Text.cpp"
#include "Engine/GUI/Button/Button.cpp"

/* Editor */
#ifndef GAME_SHIPPING
    #include "Engine/Editor/SceneEditor/SceneEditor.h"
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.h"
    #include "Engine/Core/Core.h"
#endif

/* Sources */
#ifndef GAME_SHIPPING
    #include "Engine/Editor/UiScripts/UiScripts.cpp"
    #include "Engine/Editor/Styles/Styles.cpp"
#endif

#include "Engine/Lighting/InitLighting.cpp"
#include "Engine/Lighting/skybox.cpp"
#include "Engine/Core/SaveLoad.cpp"


#ifndef GAME_SHIPPING
    #include "Engine/Editor/CodeEditor/CodeEditor.cpp"
    #include "Engine/Editor/SceneEditor/SceneEditor.cpp"
    #include "Engine/Editor/EntitiesList/EntitiesList.cpp"
#endif

#include "Engine/Core/RunGame.cpp"

#ifndef GAME_SHIPPING
    // #include "Engine/Core/PreviewProject.cpp"
    #include "Engine/Editor/MaterialsNodeEditor/MaterialsNodeEditor.cpp"
    #include "Engine/Editor/Inspector/Inspector.cpp"
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.cpp"
    #include "Engine/Core/Core.cpp"
    #include "Engine/Editor/MenuBar/MenuBar.cpp"

    /* Game Builder */
    #include "GameBuilder/builder.cpp"

#endif


#endif