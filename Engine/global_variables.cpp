#include "../include_all.h"

/* Undeclared Classes */
class Entity;


/* Not specific */

variant<Entity*, Light*> object_in_inspector;

vector<Entity> entities_list_pregame;
vector<Light> lights_list_pregame;

Light lights[MAX_LIGHTS];



/* Shaders */
Shader shader;


/* Entities List */
bool in_game_preview = false;

Texture2D run_texture;
Texture2D pause_texture;

int listViewExScrollIndex = 0;
int listViewExActive = 0;
int listViewExFocus = 0;
vector<char*> listViewExList;
vector<char*> listViewExListTypes;

bool canAddEntity = false;
bool canAddLight = false;

vector<string> objectNames;
char name[256] = { 0 };

float scale = 1;
ImVec4 color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

bool IsChildren = false;

bool showNextTime = true;
bool create = false;

bool parent_selected = false;
bool old_parent_selected = false;

bool child_selected = false;
bool old_child_selected = false;

char* selected_gameObject_type = "";

/* Inspector */
// Entity Properties
string entity_name = "";
Vector3 entity_scale = {1, 1, 1};
Vector3 entity_position = {0, 0, 0};
Vector3 entity_rotation = {0, 0, 0};
Color entity_color = RED;

// Entity Color for ImGui
ImVec4 entity_colorImGui = { 0, 0, 0, 0 };

// Entity Inspector State
bool changeEntity_colorPopup = false;
bool setentity_color = false;

// Entity Script Path and Index in Inspector
string entity_in_inspector_script_path;
int entity_in_inspector_script_path_index;

// Entity Model Path and Index in Inspector
char modelPathBuffer[512] = {0};
string entity_in_inspector_model_path;
int entity_in_inspector_model_path_index;

// Physics Properties
bool do_physics = false;

// Others
int last_entity_index = 0;



/* Code Editor [IDE] */
string code;
string code_editor_script_path;

TextEditor editor;




/* Scene Editor */

// Textures
RenderTexture2D renderTexture;
Texture2D texture;
Rectangle rectangle = { screenWidth*.2, screenHeight*.2, texture.width, texture.height };

// Camera
Camera3D scene_camera;
float lerp_factor = 0.5f;
float movementSpeed = 0.5f;
Vector3 front;

// Gizmo -> Position Arrows
bool dragging = false;
bool dragging_gizmo = false;
Vector2 mouse_drag_start = { 0, 0 };

// Gizmo -> Rotation Taurus
int gizmo_arrow_selected;
int gizmo_taurus_selected;
bool isHoveringGizmo;

// Gizmo -> Mouse Position
Vector2 mousePosition;
Vector2 mousePositionPrev = GetMousePosition();

// ImGui Window Info
float sceneEditorWindowWidth;
float sceneEditorWindowHeight;
float sceneEditorWindowX;
float sceneEditorWindowY;




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

bool first_time_gameplay = true;



/* Themes */
// Create theme
bool createNewThemeWindow_open = false;