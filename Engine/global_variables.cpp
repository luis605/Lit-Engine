#include "../include_all.h"

/* Undeclared Classes */
class Entity;


/* Not specific */

variant<Entity*, Light*> object_in_inspector;

vector<Entity> entities_list_pregame;
vector<Light> lights_list_pregame;

Entity *selected_entity;


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

bool is_create_entity_a_child = false;

bool showNextTime = true;
bool create = false;

bool parent_selected = false;
bool old_parent_selected = false;

bool child_selected = false;
bool old_child_selected = false;

char* selected_gameObject_type = "";

/* Inspector */
string entity_name = "";
Vector3 entity_scale = {1, 1, 1};
Vector3 entity_position = {0, 0, 0};
Vector3 entity_rotation = {0, 0, 0};
Color entity_color = RED;

ImVec4 entity_colorImGui = { 0, 0, 0, 0 };

bool changeEntity_colorPopup = false;
bool setentity_color = false;
string selected_entity_script_path;
int selected_entity_script_path_index;

// Entity Model Path and Index in Inspector
char modelPathBuffer[512] = {0};
int selected_entity_model_path_index;

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
        .def_readwrite("position", &Camera3D::position, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("target", &Camera3D::target, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("up", &Camera3D::up, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("fovy", &Camera3D::fovy, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("projection", &Camera3D::projection, py::call_guard<py::gil_scoped_release>());
    pybind11::object camera_obj = pybind11::cast(camera);
    camera_obj.attr("__class__") = m.attr("Camera3D");
    return camera_obj;
}

bool first_time_gameplay = true;



/* Themes */
// Create theme
bool createNewThemeWindow_open = false;

// Load && Save
string themes_folder = "project/themes/";
bool show_file_explorer = false;
bool show_save_theme_window = false;
bool show_load_theme_window = false;


const int themes_colors[] = {
    ImGuiCol_Text,
    ImGuiCol_TextDisabled,
    ImGuiCol_WindowBg,              
    ImGuiCol_ChildBg,               
    ImGuiCol_PopupBg,               
    ImGuiCol_Border,
    ImGuiCol_BorderShadow,
    ImGuiCol_FrameBg,               
    ImGuiCol_FrameBgHovered,
    ImGuiCol_FrameBgActive,
    ImGuiCol_TitleBg,
    ImGuiCol_TitleBgActive,
    ImGuiCol_TitleBgCollapsed,
    ImGuiCol_MenuBarBg,
    ImGuiCol_ScrollbarBg,
    ImGuiCol_ScrollbarGrab,
    ImGuiCol_ScrollbarGrabHovered,
    ImGuiCol_ScrollbarGrabActive,
    ImGuiCol_CheckMark,
    ImGuiCol_SliderGrab,
    ImGuiCol_SliderGrabActive,
    ImGuiCol_Button,
    ImGuiCol_ButtonHovered,
    ImGuiCol_ButtonActive,
    ImGuiCol_Header,                
    ImGuiCol_HeaderHovered,
    ImGuiCol_HeaderActive,
    ImGuiCol_Separator,
    ImGuiCol_SeparatorHovered,
    ImGuiCol_SeparatorActive,
    ImGuiCol_ResizeGrip,            
    ImGuiCol_ResizeGripHovered,
    ImGuiCol_ResizeGripActive,
    ImGuiCol_Tab,                   
    ImGuiCol_TabHovered,
    ImGuiCol_TabActive,
    ImGuiCol_TabUnfocused,
    ImGuiCol_TabUnfocusedActive,
    ImGuiCol_DockingPreview,        
    ImGuiCol_DockingEmptyBg,        
    ImGuiCol_PlotLines,
    ImGuiCol_PlotLinesHovered,
    ImGuiCol_PlotHistogram,
    ImGuiCol_PlotHistogramHovered,
    ImGuiCol_TableHeaderBg,         
    ImGuiCol_TableBorderStrong,     
    ImGuiCol_TableBorderLight,      
    ImGuiCol_TableRowBg,            
    ImGuiCol_TableRowBgAlt,         
    ImGuiCol_TextSelectedBg,
    ImGuiCol_DragDropTarget,        
    ImGuiCol_NavHighlight,          
    ImGuiCol_NavWindowingHighlight, 
    ImGuiCol_NavWindowingDimBg,     
    ImGuiCol_ModalWindowDimBg,
    ImGuiCol_COUNT
};


const char* themes_colors_string[] = {
    "Text",
    "TextDisabled",
    "WindowBg",
    "ChildBg",
    "PopupBg",
    "Border",
    "BorderShadow",
    "FrameBg",
    "FrameBgHovered",
    "FrameBgActive",
    "TitleBg",
    "TitleBgActive",
    "TitleBgCollapsed",
    "MenuBarBg",
    "ScrollbarBg",
    "ScrollbarGrab",
    "ScrollbarGrabHovered",
    "ScrollbarGrabActive",
    "CheckMark",
    "SliderGrab",
    "SliderGrabActive",
    "Button",
    "ButtonHovered",
    "ButtonActive",
    "Header",
    "HeaderHovered",
    "HeaderActive",
    "Separator",
    "SeparatorHovered",
    "SeparatorActive",
    "ResizeGrip",
    "ResizeGripHovered",
    "ResizeGripActive",
    "Tab",
    "TabHovered",
    "TabActive",
    "TabUnfocused",
    "TabUnfocusedActive",
    "DockingPreview",
    "DockingEmptyBg",
    "PlotLines",
    "PlotLinesHovered",
    "PlotHistogram",
    "PlotHistogramHovered",
    "TableHeaderBg",
    "TableBorderStrong",
    "TableBorderLight",
    "TableRowBg",
    "TableRowBgAlt",
    "TextSelectedBg",
    "DragDropTarget",
    "NavHighlight",
    "NavWindowingHighlight",
    "NavWindowingDimBg",
    "ModalWindowDimBg"
};