#include "../include_all.h"


pid_t pid;
int pipe_fds[2];
int pipe_fds_entities[2];
int pipe_fds_lights[2];

/* Not specific */
char* selected_game_object_type = "";

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

vector<string> objectNames;
char name[256] = { 0 };

float entity_create_scale = 1;
ImVec4 entity_create_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

bool is_create_entity_a_child = false;

bool showNextTime = true;
bool create = false;



/* RunGame */
LitCamera camera;

pybind11::object export_camera()
{
    pybind11::module m("raylib_camera");
    pybind11::class_<LitCamera>(m, "LitCamera")
        .def(pybind11::init<>())
        .def_readwrite("position", &LitCamera::position, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("front", &LitCamera::front, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("target", &LitCamera::target, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("up", &LitCamera::up, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("fovy", &LitCamera::fovy, py::call_guard<py::gil_scoped_release>())
        .def_readwrite("projection", &LitCamera::projection, py::call_guard<py::gil_scoped_release>());
    pybind11::object camera_obj = pybind11::cast(camera);
    camera_obj.attr("__class__") = m.attr("LitCamera");
    return camera_obj;
}


