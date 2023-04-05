#include "../include_all.h"

string colorToString(const Color& color) {
  stringstream ss;
  ss << "(" << color.r << ", " << color.g << ", " << color.b << ", " << color.a << ")";
  return ss.str();
}



string getFileExtension(string filePath)
{
    filesys::path pathObj(filePath);

    if (pathObj.has_extension()) {
        return pathObj.extension().string();
    }
    return "no file extension";
}





string read_file_to_string(const string& filename) {
    ifstream file(filename);
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}



PYBIND11_EMBEDDED_MODULE(input_module, m) {
    m.def("IsMouseButtonPressed", &IsMouseButtonPressed);
    m.def("IsKeyDown", &IsKeyDown);

    // Expose the KeyboardKey enum to Python
    py::enum_<KeyboardKey>(m, "KeyboardKey")
        .value("KEY_NULL", KEY_NULL)
        .value("KEY_APOSTROPHE", KEY_APOSTROPHE)
        .value("KEY_COMMA", KEY_COMMA)
        .value("KEY_MINUS", KEY_MINUS)
        .value("KEY_PERIOD", KEY_PERIOD)
        .value("KEY_SLASH", KEY_SLASH)
        .value("KEY_ZERO", KEY_ZERO)
        .value("KEY_ONE", KEY_ONE)
        .value("KEY_TWO", KEY_TWO)
        .value("KEY_THREE", KEY_THREE)
        .value("KEY_FOUR", KEY_FOUR)
        .value("KEY_FIVE", KEY_FIVE)
        .value("KEY_SIX", KEY_SIX)
        .value("KEY_SEVEN", KEY_SEVEN)
        .value("KEY_EIGHT", KEY_EIGHT)
        .value("KEY_NINE", KEY_NINE)
        .value("KEY_SEMICOLON", KEY_SEMICOLON)
        .value("KEY_EQUAL", KEY_EQUAL)
        .value("KEY_A", KEY_A)
        .value("KEY_B", KEY_B)
        .value("KEY_C", KEY_C)
        .value("KEY_D", KEY_D)
        .value("KEY_E", KEY_E)
        .value("KEY_F", KEY_F)
        .value("KEY_G", KEY_G)
        .value("KEY_H", KEY_H)
        .value("KEY_I", KEY_I)
        .value("KEY_J", KEY_J)
        .value("KEY_K", KEY_K)
        .value("KEY_L", KEY_L)
        .value("KEY_M", KEY_M)
        .value("KEY_N", KEY_N)
        .value("KEY_O", KEY_O)
        .value("KEY_P", KEY_P)
        .value("KEY_Q", KEY_Q)
        .value("KEY_R", KEY_R)
        .value("KEY_S", KEY_S)
        .value("KEY_T", KEY_T)
        .value("KEY_U", KEY_U)
        .value("KEY_V", KEY_V)
        .value("KEY_W", KEY_W)
        .value("KEY_X", KEY_X)
        .value("KEY_Y", KEY_Y)
        .value("KEY_Z", KEY_Z)
        .value("KEY_LEFT_BRACKET", KEY_LEFT_BRACKET)
        .value("KEY_BACKSLASH", KEY_BACKSLASH)
        .value("KEY_RIGHT_BRACKET", KEY_RIGHT_BRACKET)
        .value("KEY_GRAVE", KEY_GRAVE)
        .value("KEY_ESCAPE", KEY_ESCAPE)
        .value("KEY_ENTER", KEY_ENTER)
        .value("KEY_TAB", KEY_TAB)
        .value("KEY_BACKSPACE", KEY_BACKSPACE)
        .value("KEY_INSERT", KEY_INSERT)
        .value("KEY_DELETE", KEY_DELETE)
        .value("KEY_RIGHT", KEY_RIGHT)
        .value("KEY_LEFT", KEY_LEFT)
        .value("KEY_DOWN", KEY_DOWN)
        .value("KEY_UP", KEY_UP)
        .value("KEY_PAGE_UP", KEY_PAGE_UP)
        .value("KEY_PAGE_DOWN", KEY_PAGE_DOWN)
        .value("KEY_HOME", KEY_HOME)
        .value("KEY_END", KEY_END)
        .value("KEY_CAPS_LOCK", KEY_CAPS_LOCK)
        .value("KEY_SCROLL_LOCK", KEY_SCROLL_LOCK)
        .value("KEY_NUM_LOCK", KEY_NUM_LOCK)
        .value("KEY_PRINT_SCREEN", KEY_PRINT_SCREEN)
        .value("KEY_PAUSE", KEY_PAUSE)
        .value("KEY_F1", KEY_F1)
        .value("KEY_F2", KEY_F2)
        .value("KEY_F3", KEY_F3)
        .value("KEY_F4", KEY_F4)
        .value("KEY_F5", KEY_F5)
        .value("KEY_F6", KEY_F6)
        .value("KEY_F7", KEY_F7)
        .value("KEY_F8", KEY_F8)
        .value("KEY_F9", KEY_F9)
        .value("KEY_F10", KEY_F10)
        .value("KEY_F11", KEY_F11)
        .value("KEY_F12", KEY_F12)
        .value("KEY_LEFT_SHIFT", KEY_LEFT_SHIFT)
        .value("KEY_LEFT_CONTROL", KEY_LEFT_CONTROL)
        .value("KEY_LEFT_ALT", KEY_LEFT_ALT)
        .value("KEY_LEFT_SUPER", KEY_LEFT_SUPER)
        .value("KEY_RIGHT_SHIFT", KEY_RIGHT_SHIFT)
        .value("KEY_RIGHT_CONTROL", KEY_RIGHT_CONTROL)
        .value("KEY_RIGHT_ALT", KEY_RIGHT_ALT)
        .value("KEY_RIGHT_SUPER", KEY_RIGHT_SUPER)
        .value("KEY_KB_MENU", KEY_KB_MENU);
}









bool raycast(Vector3 origin, Vector3 direction, bool debug);

PYBIND11_EMBEDDED_MODULE(collisions_module, m) {
    py::class_<Vector3>(m, "Vector3")
        .def(py::init<float, float, float>())
        .def_readwrite("x", &Vector3::x)
        .def_readwrite("y", &Vector3::y)
        .def_readwrite("z", &Vector3::z);
    
    m.def("raycast", &raycast, py::arg("origin"), py::arg("direction"), py::arg("debug")=false);
}



PYBIND11_EMBEDDED_MODULE(camera_module, m) {
    py::class_<Camera3D>(m, "Camera3D")
        .def(py::init<int, float, float, float>())
        .def_readwrite("position", &Camera3D::position)
        .def_readwrite("target", &Camera3D::target)
        .def_readwrite("up", &Camera3D::up)
        .def_readwrite("fovy", &Camera3D::fovy)
        .def_readwrite("projection", &Camera3D::projection);
}




py::scoped_interpreter guard{}; // Start interpreter








bool EntityRunScriptFirstTime = true;



/* Entity */
class Entity {
public:
    bool initialized = false;
    string name = "Entity";
    Color color;
    float size = 1;
    Vector3 position = { 0, 0, 0 };
    Vector3 rotation;
    Vector3 scale = { 1, 1, 1 };

    Vector3 relative_position = { 0, 0, 0 };
    Vector3 relative_rotation;
    Vector3 relative_scale = { 1, 1, 1 };

    string script = "";
    string model_path = "";
    Model model;

    bool collider = true;
    bool visible = true;
    bool isChildren = false;
    bool isParent = false;

    vector<Entity*> children;

    Entity(Color color = { 255, 255, 255, 255 }, Vector3 scale = { 1, 1, 1 }, Vector3 rotation = { 0, 0, 0 }, string name = "entity", Vector3 position = {0, 0, 0}, string script = "")
        : color(color), scale(scale), rotation(rotation), name(name), position(position), script(script)
    {
        initialized = true;
    }


    void addChild(Entity& child) {
        Entity* newChild = new Entity(child);
        newChild->relative_position = Vector3Subtract(newChild->position, this->position);
        children.push_back(newChild);
    }






    void update()
    {
        if (children.empty()) return;
        int a = 0;
        for (Entity* child : children)
        {
            child->position = Vector3Add(this->position, child->relative_position);
            child->draw();


            a++;
            child->update();
        }
    }


    void remove() {
        entities_list_pregame.erase(remove_if(entities_list_pregame.begin(), entities_list_pregame.end(),
        [this](const Entity& entity) {
        return entity.getName() == this->name;
        }),
        entities_list_pregame.end());
    }

    void setColor(Color newColor) {
        color = newColor;
    }

    void setName(const string& newName) {
        name = newName;
    }

    string getName() const {
        return name;
    }

    void setScale(Vector3 newScale) {
        scale = newScale;
    }

    // Initialize the model with a cube mesh
    void initializeModel() {
        // Create a mesh for a cube
        Mesh mesh = GenMeshCube(scale.x, scale.y, scale.z);

        // Create a model from the mesh
        model = LoadModelFromMesh(mesh);
    }

    // Load a model from a file and set its material properties
    void loadModel(const char* filename, const char* textureFilename = NULL) {
        model = LoadModel(filename);
    }


    void setModel(char* modelPath)
    {
        model = LoadModel(modelPath);
    }

    bool hasModel()
    {

        if (model.meshCount > 0)
        {
            // model has been initialized with data
            return true;
        }
        else
        {
            // model has not been initialized with data
            return false;
        }
    }

    void setShader(Shader shader)
    {
        model.materials[0].shader = shader;
    }


    void runScript()
    {
        if (script.empty()) return;

        py::gil_scoped_release release;
        py::gil_scoped_acquire acquire;

        py::module entity_module("entity_module");
        py::class_<Entity>(entity_module, "Entity")
            .def(py::init<>())
            .def_readwrite("name", &Entity::name)
            .def_readwrite("position", &Entity::position)
            .def_readwrite("scale", &Entity::scale)
            .def_readwrite("rotation", &Entity::rotation)
            .def_readwrite("color", &Entity::color)
            .def_readwrite("visible", &Entity::visible)
            .def_readwrite("collider", &Entity::collider);

        py::object entity_obj = py::cast(this);
        py::module input_module = py::module::import("input_module");
        py::module collisions_module = py::module::import("collisions_module");
        py::module camera_module = py::module::import("camera_module");

        if (IsKeyDown(KEY_B))
        {
            camera.position.x += .1;
        }


        auto locals = py::dict("entity"_a=entity_obj,
                            "IsMouseButtonPressed"_a=input_module.attr("IsMouseButtonPressed"),
                            "IsKeyDown"_a=input_module.attr("IsKeyDown"),
                            "KeyboardKey"_a=input_module.attr("KeyboardKey"),
                            "raycast"_a=collisions_module.attr("raycast"),
                            "camera"_a=py::cast(&camera));



        try {
            pybind11::gil_scoped_acquire acquire;
            string script_content = read_file_to_string(script);
            while (true)
            {
                py::exec(script_content, py::globals(), locals);

            }
            pybind11::gil_scoped_release release;
        } catch (const py::error_already_set& e) {
            py::print(e.what());
        }

    }


        // py::module entity_module("entity_module");
        // py::class_<Entity>(entity_module, "Entity")
        //     .def(py::init<>())
        //     .def_readwrite("name", &Entity::name)
        //     .def_readwrite("position", &Entity::position)
        //     .def_readwrite("scale", &Entity::scale)
        //     .def_readwrite("rotation", &Entity::rotation)
        //     .def_readwrite("color", &Entity::color)
        //     .def_readwrite("visible", &Entity::visible)
        //     .def_readwrite("collider", &Entity::collider);
        


        // py::object entity_obj = py::cast(this);

        // cout << "pass here 4.1" << endl;
        // py::module input_module = py::module::import("input_module");
        // cout << "pass here 4.2" << endl;
        // py::module collisions_module = py::module::import("collisions_module");
        // cout << "pass here 4.3" << endl;
        // py::module camera_module = py::module::import("camera_module");
        // cout << "pass here 4.4" << endl;

        // if (IsKeyDown(KEY_B))
        // {
        //     camera.position.x += .1;
        // }

        // auto locals = py::dict("entity"_a=entity_obj,
        //                     "IsMouseButtonPressed"_a=input_module.attr("IsMouseButtonPressed"),
        //                     "IsKeyDown"_a=input_module.attr("IsKeyDown"),
        //                     "KeyboardKey"_a=input_module.attr("KeyboardKey"),
        //                     "raycast"_a=collisions_module.attr("raycast"),
        //                     "camera"_a=py::cast(&camera));

        // cout << "pass here 5" << endl;


        // try {
        //     pybind11::gil_scoped_acquire acquire;
        //     string script_content = read_file_to_string(script);
        //     while (true)
        //     {
        //         py::exec(script_content, py::globals(), locals);
        //     }
        //     pybind11::gil_scoped_release release;
        // } catch (const py::error_already_set& e) {
        //     py::print(e.what());
        // }
        // cout << "pass here 6" << endl;

    // }


    // Draw the model
    void draw() {
        if (!hasModel())
        {
            initializeModel();
        }

        update();
        
        model.transform = MatrixScale(scale.x, scale.y, scale.z);
        DrawModel(model, {position.x, position.y, position.z}, 1.0f, color);
    }



};

    



float GetExtremeValue(const Vector3& a) {
    return max(max(abs(a.x), abs(a.y)), abs(a.z));
}


bool raycast(Vector3 origin, Vector3 direction, bool debug)
{
    Ray ray;
    ray.position = origin;
    ray.direction = direction;

    if (debug)
    {
        DrawRay(ray, RED);
    }

    Entity entity;
    for (int index = 0; index < entities_list.size(); index++)
    {
        entity = entities_list[index];

        if (!entity.collider)
        {
            continue;
        }   

        float extreme_rotation = GetExtremeValue(entity.rotation);

        Matrix matScale = MatrixScale(entity.scale.x, entity.scale.y, entity.scale.z);
        Matrix matRotation = MatrixRotate(entity.rotation, extreme_rotation*DEG2RAD);
        Matrix matTranslation = MatrixTranslate(entity.position.x, entity.position.y, entity.position.z);

        Matrix modelMatrix = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);

        Vector2 pos = { GetMousePosition().x - sceneEditorWindowX, GetMousePosition().y - sceneEditorWindowY };
        Vector2 realPos = { pos.x * GetScreenWidth()/rectangle.width, pos.y * GetScreenHeight()/rectangle.height };        

        RayCollision meshHitInfo = { 0 };


        for (int mesh_i = 0; mesh_i < entity.model.meshCount; mesh_i++)
        {
            meshHitInfo = GetRayCollisionMesh(ray, entity.model.meshes[mesh_i], modelMatrix);
            if (meshHitInfo.hit)
            {
                return true;
            }
        }

        if (meshHitInfo.hit) return true;
    }

    return false;
}