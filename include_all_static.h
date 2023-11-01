#ifndef INCLUDE_ALL_STATIC_H_
#define INCLUDE_ALL_STATIC_H_

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "include/raylib.h"
#include "include/raymath.h"
#include "include/rcamera.h"
#include "include/rlgl.h"
#include "include/custom.h"
#include "include/rlFrustum.cpp"

#include "dependencies/include/glad/glad.h"

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
        #include <libavcodec/avcodec.h>
        #include <libavformat/avformat.h>
        #include <libswscale/swscale.h>
}
#endif

#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <exception>
#include <signal.h>
#include "dependencies/include/glm/glm.hpp"
#include "dependencies/include/glm/gtc/matrix_transform.hpp"
#include <algorithm>
#include <cmath>

#include <limits>
#include <omp.h>

#ifdef _WIN32
//    #include <Python.h>
#else
    #include <python3.11/Python.h>
#endif

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>
#include <regex>

#include "include/nlohmann/json.hpp"

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

std::map<std::string, const char*> scriptMap;


/* NameSpaces */
using namespace std;
using std::vector;

namespace fs = std::filesystem;
namespace py = pybind11;
using namespace py::literals;

using json = nlohmann::json;

// Physics
#include "Engine/Physics/InitPhysx.cpp"

// Critical
#include "globals.h"
#include "Engine/Core/LoD.cpp"
#include "Engine/Scripting/math.cpp"

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

#include "Engine/Core/SaveLoad.cpp"
#include "Engine/Lighting/InitLighting.cpp"
#include "Engine/Lighting/skybox.cpp"


#ifndef GAME_SHIPPING
    #include "Engine/Editor/CodeEditor/CodeEditor.cpp"
    #include "Engine/Editor/SceneEditor/SceneEditor.cpp"
    #include "Engine/Editor/EntitiesList/EntitiesList.cpp"
#endif

#include "Engine/Core/RunGame.cpp"

#ifndef GAME_SHIPPING
    #include "Engine/Core/PreviewProject.cpp"
    #include "Engine/Editor/MaterialsNodeEditor/MaterialsNodeEditor.cpp"
    #include "Engine/Editor/Inspector/Inspector.cpp"
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.cpp"
    #include "Engine/Core/Core.cpp"
    #include "Engine/Editor/MenuBar/MenuBar.cpp"

    /* Game Builder */
    #include "GameBuilder/builder.cpp"

#endif


#endif