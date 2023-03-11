#ifndef INCLUDE_ALL_H_
#define INCLUDE_ALL_H_

#include "raylib.h"
#include "raymath.h"

#define RLIGHTS_IMPLEMENTATION
#include "include/rlights.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"
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
#include "json.hpp"

using namespace std;
using std::vector;

namespace py = pybind11;
using namespace py::literals;

#include "globals.h"
#include "Engine/global_variables.cpp"
#include "Engine/functions.cpp"

// Define Functions
void RunGame();
void Inspector();

#include "Engine/Engine.h"
#include "Engine/RunGame.h"
#include "Engine/Ui/AssetsExplorer.h"

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
