#include "../include_all.h"





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


void AddLight()
{
    if (canAddLight)
    {
        cout << "AddLight" << endl;
        Light light_create = NewLight((Vector3){ -2, 1, -2 }, RED);
        lights_list_pregame.push_back(light_create);
        canAddLight = false;
    }
}





py::scoped_interpreter guard{}; // Start interpreter











bool EntityRunScriptFirstTime = true;
bool Entity_already_registered = false;

std::mutex script_mutex;


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
    Vector3 relative_rotation = { 0, 0, 0 };
    Vector3 relative_scale = { 1, 1, 1 };

    string script = "";
    string model_path = "";
    Model model;

    BoundingBox bounds;


    std::filesystem::path texture_path;
    Texture2D texture;

    std::filesystem::path normal_texture_path;
    Texture2D normal_texture;

    bool collider = true;
    bool visible = true;
    bool isChild = false;
    bool isParent = false;
    bool running = false;

    int id = "";

    Entity* parent;
    vector<Entity*> children;


    Entity(Color color = { 255, 255, 255, 255 }, Vector3 scale = { 1, 1, 1 }, Vector3 rotation = { 0, 0, 0 }, string name = "entity", Vector3 position = {0, 0, 0}, string script = "")
        : color(color), scale(scale), rotation(rotation), name(name), position(position), script(script)
    {   
        initialized = true;
    }

    bool operator==(const Entity& other) const {
        return this->id == other.id;
    }


    void addChild(Entity& child) {
        Entity* newChild = new Entity(child);
        newChild->relative_position = Vector3Subtract(newChild->position, this->position);
        newChild->parent = selected_entity;
        printf("Parent x position: %f", newChild->parent->position.x);
        children.push_back(newChild);
    }

    void update_children()
    {
        if (children.empty()) return;
        for (Entity* child : children)
        {
            child->render();
            if (child == selected_entity) return;

            child->position = Vector3Add(this->position, child->relative_position);

            child->update_children();

        }
    }


    void remove() {

        for (Entity* child : children) {
            delete child;
        }
        children.clear();


        entities_list_pregame.erase(
            std::remove_if(entities_list_pregame.begin(), entities_list_pregame.end(),
                [this](const Entity& entity) {
                    return entity.id == this->id;
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

    void initializeDefaultModel() {
        Mesh mesh = GenMeshCube(scale.x, scale.y, scale.z);
        model = LoadModelFromMesh(mesh);
    }

    void loadModel(const char* filename, const char* textureFilename = NULL) {
        model = LoadModel(filename);
    }


    void setModel(char* modelPath)
    {
        model_path = modelPath;
        model = LoadModel(modelPath);
        model.materials[0].shader = shader;
        if (IsTextureReady(this->texture))
        {
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
        }

        bounds = GetMeshBoundingBox(model.meshes[0]);
    }

    bool hasModel()
    {

        if (model.meshCount > 0)
            return true;
        else
            return false;
    }

    void setShader(Shader shader)
    {
        model.materials[0].shader = shader;
    }

    void runScript(std::reference_wrapper<Entity> entityRef, Camera3D* rendering_camera)
    {
        if (script.empty()) return;
        running = true;
        std::lock_guard<std::mutex> lock(script_mutex);
        
        py::gil_scoped_release release;
        py::gil_scoped_acquire acquire;

        if (!Entity_already_registered)
        {
            Entity_already_registered = true;
            py::module entity_module("entity_module");
            py::class_<Entity>(entity_module, "Entity")
                .def(py::init<>())
                .def_readwrite("name", &Entity::name, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("position", &Entity::position, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("scale", &Entity::scale, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("rotation", &Entity::rotation, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("color", &Entity::color, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("visible", &Entity::visible, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("id", &Entity::id, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("collider", &Entity::collider, py::call_guard<py::gil_scoped_release>());
        }
        Entity& this_entity = entityRef.get();

        py::object entity_obj = py::cast(&this_entity);
        py::module input_module = py::module::import("input_module");
        py::module collisions_module = py::module::import("collisions_module");
        py::module camera_module = py::module::import("camera_module");
        py::module time_module = py::module::import("time_module");
        py::module color_module = py::module::import("color_module");
        py::module math_module = py::module::import("math_module");
        
        rendering_camera->position.x = 100;
        std::cout << "Camera position: " << camera.position.x << std::endl;

        thread_local py::dict locals = py::dict(
            "entity"_a = entity_obj,
            "IsMouseButtonPressed"_a = input_module.attr("IsMouseButtonPressed"),
            "IsKeyDown"_a = input_module.attr("IsKeyDown"),
            "KeyboardKey"_a = input_module.attr("KeyboardKey"),
            "raycast"_a = collisions_module.attr("raycast"),
            "Vector3"_a = collisions_module.attr("Vector3"),
            "Color"_a = color_module.attr("Color"),
            "time"_a = py::cast(&time_instance),
            "lerp"_a = math_module.attr("lerp"),
            "camera"_a = py::cast(rendering_camera)
        );


        try {
            pybind11::gil_scoped_acquire acquire;
            string script_content = read_file_to_string(script);
            

            py::module module("__main__");

            for (auto item : locals) {
                module.attr(item.first) = item.second;
            }

            py::eval<py::eval_statements>(script_content, module.attr("__dict__"));


            if (module.attr("__dict__").contains("update")) {
                py::object update_func = module.attr("update");


                script_mutex.unlock();
                
                /*
                We only want to call the update function every frame, but because the runScript()
                is being called in a new thread the update_func may be called more than once per
                frame. This solution will fix it.
                */

                float last_frame_count = 0;
                while (running) {
                    if (time_instance.dt - last_frame_count != 0) {
                        locals["time"] = py::cast(&time_instance);
                        update_func();
                        last_frame_count = time_instance.dt;
                    }
                }
            } else {
                std::cout << "The 'update' function is not defined in the script.\n";
                return 1;
            }

            py::gil_scoped_release release;
        } catch (const py::error_already_set& e) {
            py::print(e.what());
        }
    }




    void render() {
        if (!hasModel())
            initializeDefaultModel();

        update_children();

        model.transform = MatrixScale(scale.x, scale.y, scale.z);
        if (visible)
            DrawModel(model, {position.x, position.y, position.z}, 1.0f, color);
    }
};

    

py::module entity_module("entity_module");



float GetExtremeValue(const Vector3& a) {
    const float absX = abs(a.x);
    const float absY = abs(a.y);
    const float absZ = abs(a.z);

    return max(max(absX, absY), absZ);
}


HitInfo raycast(Vector3 origin, Vector3 direction, bool debug=false, std::vector<Entity> ignore = {})
{
    std::lock_guard<std::mutex> lock(script_mutex);
    pybind11::gil_scoped_acquire acquire;

    HitInfo _hitInfo;
    _hitInfo.hit = false;

    Ray ray;
    ray.position = origin;
    ray.direction = direction;

    if (debug)
        DrawRay(ray, RED);


    Entity entity;

    for (int index = 0; index < entities_list.size(); index++)
    {
        entity = entities_list[index];

        if (std::find(ignore.begin(), ignore.end(), entity) != ignore.end())
            continue;

        if (!entity.collider)
            continue;

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
                _hitInfo.hit = true;
                _hitInfo.distance = meshHitInfo.distance;
                _hitInfo.entity = &entity;
                _hitInfo.worldPoint = meshHitInfo.point;
                _hitInfo.worldNormal = meshHitInfo.normal;
               _hitInfo.hitColor = entity.color;

                return _hitInfo;
            }
        }
    }

    pybind11::gil_scoped_release release;
    
    return _hitInfo;
}