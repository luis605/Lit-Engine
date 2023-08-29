#ifndef INCLUDE_ALL_H_
#define INCLUDE_ALL_H_

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "include/raylib.h"
#include "include/raymath.h"
#include "include/rcamera.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"

#include "dependencies/include/glad/glad.h"

#include "include/bullet3/src/BulletCollision/BroadphaseCollision/btBroadphaseProxy.h"

#include "include/bullet3/src/BulletCollision/CollisionShapes/btCollisionShape.h"
#include "include/bullet3/src/BulletCollision/CollisionShapes/btConvexPolyhedron.h"
#include "include/bullet3/src/BulletCollision/CollisionShapes/btShapeHull.h"

#include "include/bullet3/src/LinearMath/btVector3.h"
#include "include/bullet3/src/btBulletDynamicsCommon.h"

#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <exception>
#include "dependencies/include/glm/glm.hpp"
#include "dependencies/include/glm/gtc/matrix_transform.hpp"
#include <algorithm>
#include <cmath>
#include <python3.11/Python.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>
#include <boost/filesystem.hpp>

#include "include/nlohmann/json.hpp"

#include <thread>
#include <unordered_map>


/* NameSpaces */
using namespace std;
using std::vector;

namespace py = pybind11;
using namespace py::literals;

using json = nlohmann::json;

namespace filesys = boost::filesystem;


// Physics
#include "Engine/Physics/InitPhysx.cpp"

// Critical
#include "Engine/Scripting/math.cpp"

/* Game Objects */
#include "Engine/GUI/Text/Text.h"
#include "Engine/GUI/Button/Button.h"
#include "Engine/Core/Engine.hpp"
#include "Engine/Lighting/lights.h"

/* Scripting */
#include "Engine/Scripting/time.cpp"
#include "Engine/Scripting/functions.cpp"


/* Globals */
#include "Engine/Core/functions.h"
#include "Engine/Tests/global_variables.cpp"
#include "Engine/Core/Engine.cpp"


/* GUI */
#include "Engine/GUI/Tooltip/Tooltip.cpp"
#include "Engine/GUI/Text/Text.cpp"
#include "Engine/GUI/Button/Button.cpp"

/* Sources */
#include "Engine/Core/SaveLoad.cpp"
#include "Engine/Lighting/InitLighting.cpp"
#include "Engine/Lighting/skybox.cpp"

/* Game Builder */
#include "GameBuilder/builder.cpp"


#endif
