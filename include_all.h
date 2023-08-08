#ifndef INCLUDE_ALL_H_
#define INCLUDE_ALL_H_

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "include/raylib.h"
#include "include/raymath.h"
#include "include/rcamera.h"
#include "include/rlgl.h"
#include "include/custom.h"

#ifndef GAME_SHIPPING
    #include "imgui/imgui.h"
    #include "imgui/imgui_internal.h"
    #include "include/rlImGui.h"
    #include "ImGuiColorTextEdit/TextEditor.h"

    #include "include/ImNodes.h"
    #include "include/ImNodesEz.h"
#endif

#include "dependencies/include/glad/glad.h"

// #include <PxPhysicsAPI.h>
// #include <PxConfig.h>

#include <stdio.h>
#include <iostream>
#include <dirent.h>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <exception>
#include <signal.h>
#include <boost/filesystem.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <sys/mman.h>
#include <python3.11/Python.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>
#include <regex>

#ifndef GAME_SHIPPING
    #include <json.hpp>
#else
    #include "include/nlohmann/json.hpp"
#endif

#include <IconsFontAwesome.h>
#include <execinfo.h>
#include <unistd.h>
#include <typeinfo>
#include <thread>
#include <functional>
#include <future>
#include <iomanip>
#include <unordered_map>
#include <map>

/* Physx */


/* NameSpaces */
using namespace std;
using std::vector;

namespace filesys = boost::filesystem;

namespace py = pybind11;
using namespace py::literals;

using json = nlohmann::json;

// using namespace physx;

/* Game Objects */
#include "Engine/Core/Engine.hpp"
#include "Engine/Lighting/lights.h"

/* Globals */
#include "globals.h"
#include "Engine/Core/global_variables.cpp"
#include "Engine/Core/functions.h"

#include "Engine/Scripting/functions.cpp"
#include "Engine/Core/Engine.cpp"

#include "Engine/Core/RunGame.h"

/* GUI */
#include "Engine/GUI/Tooltip/Tooltip.cpp"
#include "Engine/GUI/Text/Text.cpp"
#include "Engine/GUI/Button/Button.cpp"

/* Editor */
#ifndef GAME_SHIPPING
    #include "Engine/Editor/AssetsExplorer/AssetsExplorer.h"
    #include "Engine/Core/Core.h"
#endif

/* Sources */
// #include "Engine/Physics/InitPhysx.cpp"
#ifndef GAME_SHIPPING
    #include "Engine/Editor/UiScripts/UiScripts.cpp"
    #include "Engine/Editor/Styles/Styles.cpp"
#endif

#include "Engine/Core/SaveLoad.cpp"
#include "Engine/Lighting/InitLighting.cpp"
#include "Engine/Lighting/skybox.cpp"


#ifndef GAME_SHIPPING
    #include "Engine/Editor/CodeEditor/CodeEditor.cpp"
    #include "Engine/Editor/EntitiesList/EntitiesList.cpp"
    #include "Engine/Editor/SceneEditor/SceneEditor.cpp"
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
