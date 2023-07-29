#ifndef INCLUDE_H
#define INCLUDE_H

#define GAME_SHIPPING

#include <thread>
#include <vector>
#include <string>
#include <python3.11/Python.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <mutex>
#include <iostream>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <sstream>
#include <dirent.h>

#include "../include/raylib.h"
#include "../include/raymath.h"
#include "../include/rcamera.h"
#include "../include/rlgl.h"
#include "../include/custom.h"
#include "../include/nlohmann/json.hpp"

using namespace std;
using std::vector;

namespace filesys = boost::filesystem;

namespace py = pybind11;
using namespace py::literals;

using json = nlohmann::json;

#include "../Engine/SaveLoad.cpp"
#include "../Engine/Scripting/functions.cpp"
#include "../Engine/Lighting/lights.h"
#include "../Engine/RunGame.h"
#include "../Engine/RunGame.cpp"
#include "../Engine/Engine.cpp"



Camera3D camera;
vector <Entity> entities_list;

#endif // INCLUDE_H