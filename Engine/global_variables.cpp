#include "../include_all.h"

/* Undeclared Classes */
class Entity;

/* Entities List */
bool in_game_preview = false;
Entity *entity_in_inspector;

Texture2D run_texture;
Texture2D pause_texture;

int listViewExScrollIndex = 0;
int listViewExActive = 0;
int listViewExFocus = 0;
std::vector<char*> listViewExList;
bool canAddEntity = false;

std::vector<std::string> entityNames;
char name[256] = { 0 };

float scale = 1;
ImVec4 color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

bool showNextTime = true;
bool create = false;


/* Inspector */
char modelPathBuffer[512] = {0};

Vector3 entity_scale = {1, 1, 1};
Vector3 entity_position = {0, 0, 0};
Vector3 entity_rotation = {0, 0, 0};

string entity_name = "";

Color entity_color = RED;
ImVec4 entity_colorImGui = { 0, 0, 0, 0 };

bool changeentity_colorPopup = false;
bool setentity_color = false;

std::string entity_in_inspector_script_path;
int entity_in_inspector_script_path_index;

std::string entity_in_inspector_model_path;
int entity_in_inspector_model_path_index;


int last_entity_index = 0;



/* Code Editor [IDE] */
std::string code;




/* Scene Camera */

// Physics
bool do_physics = false;

// Textures
RenderTexture2D renderTexture;
Texture2D texture;
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

// Camera
Camera3D scene_camera;
float lerp_factor = 0.5f;
float movementSpeed = 0.5f;

bool dragging = false;

Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();
Vector3 front;
// ImGui Window Info
float windowWidth;
float windowHeight;
float windowX;
float windowY;




/* RunGame */
Camera3D camera;

pybind11::object export_camera()
{
    pybind11::module m("raylib_camera");
    pybind11::class_<Camera3D>(m, "Camera3D")
        .def(pybind11::init<>())
        .def_readwrite("position", &Camera3D::position)
        .def_readwrite("target", &Camera3D::target)
        .def_readwrite("up", &Camera3D::up)
        .def_readwrite("fovy", &Camera3D::fovy)
        .def_readwrite("projection", &Camera3D::projection);
    pybind11::object camera_obj = pybind11::cast(camera);
    camera_obj.attr("__class__") = m.attr("Camera3D");
    return camera_obj;
}