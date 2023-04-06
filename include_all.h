#ifndef INCLUDE_ALL_H_
#define INCLUDE_ALL_H_

#include "include/raylib.h"
#include "include/raymath.h"
#include "include/rcamera.h"


#define RLIGHTS_IMPLEMENTATION
#include "include/rlights.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"
#include "ImGuiColorTextEdit/TextEditor.h"

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
/* Physx */
//#include "PxPhysicsAPI.h"


/* NameSpaces */
using namespace std;
using std::vector;

namespace filesys = boost::filesystem;

namespace py = pybind11;
using namespace py::literals;

/* Globals */
#include "globals.h"
#include "Engine/global_variables.cpp"
#include "Engine/functions.cpp"

/* Headers */
#include "Engine/Engine.h"
#include "Engine/RunGame.h"
#include "Engine/Ui/AssetsExplorer.h"

/* Sources */
#include "Engine/Ui/UiScripts.cpp"
#include "Engine/Ui/Styles.cpp"
#include "Engine/Engine.cpp"
#include "Engine/SaveLoad.cpp"
#include "Engine/Ui/CodeEditor.cpp"
#include "Engine/Ui/EntitiesList.cpp"
#include "Engine/Ui/SceneEditor.cpp"
#include "Engine/RunGame.cpp"
#include "Engine/Ui/Inspector.cpp"
#include "Engine/Ui/AssetsExplorer.cpp"
#include "Engine/Ui/MenuBar.cpp"


using namespace std;


#endif
