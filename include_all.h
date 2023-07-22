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

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"
#include "ImGuiColorTextEdit/TextEditor.h"

#include "dependencies/include/glad/glad.h"


#include "include/ImNodes.h"
#include "include/ImNodesEz.h"

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
#include <json.hpp>
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
#include <shared_mutex>

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
#include "Engine/Engine.hpp"
#include "Engine/Lighting/lights.h"

/* Globals */
#include "globals.h"
#include "Engine/global_variables.cpp"
#include "Engine/functions.h"

#include "Engine/Engine.cpp"

/* Headers */
#include "Engine/RunGame.h"
#include "Engine/Ui/AssetsExplorer.h"

/* Sources */
// #include "Engine/Physics/InitPhysx.cpp"
#include "Engine/Ui/UiScripts.cpp"
#include "Engine/Ui/Styles.cpp"
#include "Engine/SaveLoad.cpp"
#include "Engine/Lighting/InitLighting.cpp"
#include "Engine/Ui/CodeEditor.cpp"
#include "Engine/Ui/EntitiesList.cpp"
#include "Engine/Ui/SceneEditor.cpp"
#include "Engine/RunGame.cpp"
#include "Engine/Ui/MaterialsNodeEditor.cpp"
#include "Engine/Ui/Inspector.cpp"
#include "Engine/Ui/AssetsExplorer.cpp"
#include "Engine/Core.cpp"
#include "Engine/Ui/MenuBar.cpp"



using namespace std;


#endif
