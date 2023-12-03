#include "../../include_all.h"

float GetExtremeValue(const Vector3& a) {
    const float absX = abs(a.x);
    const float absY = abs(a.y);
    const float absZ = abs(a.z);

    return max(max(absX, absY), absZ);
}


string getFileExtension(string filePath)
{
    fs::path pathObj = filePath;

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

py::scoped_interpreter guard{}; 


RLFrustum cameraFrustum;


bool EntityRunScriptFirstTime = true;
bool Entity_already_registered = false;


void InitFrustum()
{
    cameraFrustum = RLFrustum();
}

void UpdateFrustum()
{
    cameraFrustum.Extract();
}

bool PointInFrustum(const Vector3& point)
{
    return cameraFrustum.PointIn(point);
}

bool SphereInFrustum(const Vector3& position, float radius)
{
    return cameraFrustum.SphereIn(position, radius);
}

bool AABBoxInFrustum(const Vector3& min, const Vector3& max)
{
    return cameraFrustum.AABBoxIn(min, max);
}


py::module entity_module("entity_module");


std::mutex script_mutex;
class Entity {
public:
    bool initialized = false;
    string name = "Entity";
    float size = 1;
    LitVector3 position = { 0, 0, 0 };
    LitVector3 rotation = { 0, 0, 0 };
    LitVector3 scale = { 1, 1, 1 };

    LitVector3 relative_position = { 0, 0, 0 };
    LitVector3 relative_rotation = { 0, 0, 0 };
    LitVector3 relative_scale = { 1, 1, 1 };

    string script = "";
    string script_index = "";
    string model_path = "";
    Model model;

    BoundingBox bounds;


    fs::path texture_path;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> texture;


    fs::path normal_texture_path;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> normal_texture;

    fs::path roughness_texture_path;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> roughness_texture;

    fs::path ao_texture_path;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> ao_texture;

    fs::path surface_material_path;
    SurfaceMaterial surface_material;

    float tiling[2] = { 1.0f, 1.0f };


    bool collider = true;
    bool visible = true;
    bool isChild = false;
    bool isParent = false;
    bool running = false;
    bool running_first_time = false;
    bool calc_physics = false;
    bool isDynamic = false;

    typedef enum ObjectTypeEnum
    {
        ObjectType_None,
        ObjectType_Cube,
        ObjectType_Cone,
        ObjectType_Cylinder,
        ObjectType_Plane,
        ObjectType_Sphere,
        ObjectType_Torus
    };

    ObjectTypeEnum ObjectType;

    float mass = 1;
    Vector3 inertia = {0, 0, 0};

    Model LodModels[4] = { };

    int id = 0;

    Entity* parent = nullptr;
    vector<variant<Entity*, Light*, Text*, LitButton*>> children;

    enum CollisionShapeType
    {
        Box           = 0,
        HighPolyMesh  = 1,
        LowPolyMesh   = 2,
        Sphere        = 3,
        None          = 4
    };

    std::shared_ptr<CollisionShapeType> currentCollisionShapeType = make_shared<CollisionShapeType>(None);

private:
    btCollisionShape* staticBoxShape               = nullptr;
    btCollisionShape* dynamicBoxShape              = nullptr;
    btConvexHullShape* customMeshShape             = nullptr;
    btDefaultMotionState* boxMotionState           = nullptr;
    btTriangleMesh* triangleMesh                   = nullptr;
    std::shared_ptr<btRigidBody*>(boxRigidBody)    = make_shared<btRigidBody*>(nullptr);
    std::shared_ptr<btRigidBody*>(treeRigidBody)   = make_shared<btRigidBody*>(nullptr);
    LitVector3 backupPosition                      = position;
    vector<Entity*> instances;
    Matrix *transforms                             = nullptr;
    Material matInstances;
    int lastIndexCalculated                        = -1;
    Shader* entity_shader;
    bool lodEnabled                                = true;


public:
    Entity(LitVector3 scale = { 1, 1, 1 }, LitVector3 rotation = { 0, 0, 0 }, string name = "entity",
    LitVector3 position = {0, 0, 0}, string script = "")
        : scale(scale), rotation(rotation), name(name), position(position), script(script)
    {   
        initialized = true;

    }

    Entity(const Entity& other) {
        if (!this || this == nullptr || !other.initialized)
            return;
        this->initialized = other.initialized;
        this->name = other.name;
        this->size = other.size;
        this->position = other.position;
        this->rotation = other.rotation;
        this->scale = other.scale;
        this->relative_position = other.relative_position;
        this->relative_rotation = other.relative_rotation;
        this->relative_scale = other.relative_scale;
        this->script = other.script;
        this->script_index = other.script_index;
        this->model_path = other.model_path;
        this->ObjectType = other.ObjectType;
        this->model = other.model;
        this->bounds = other.bounds;
        this->tiling[0] = other.tiling[0];
        this->tiling[1] = other.tiling[1];

        this->currentCollisionShapeType     = make_shared<CollisionShapeType>(*other.currentCollisionShapeType);
        if (other.boxRigidBody && *other.boxRigidBody != nullptr)
            this->boxRigidBody              = make_shared<btRigidBody *>(*other.boxRigidBody);
        else
            this->boxRigidBody              = make_shared<btRigidBody *>(nullptr);

        if (other.treeRigidBody && *other.treeRigidBody != nullptr)
            this->treeRigidBody             = make_shared<btRigidBody *>(*other.treeRigidBody);
        else
            this->treeRigidBody             = make_shared<btRigidBody *>(nullptr);

        this->texture_path                  = other.texture_path;
        this->texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity texture variant");
        }, other.texture);


        this->normal_texture_path = other.normal_texture_path;
        this->normal_texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity normal texture variant");
        }, other.normal_texture);


        this->roughness_texture_path = other.roughness_texture_path;
        this->roughness_texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity roughness texture variant");
        }, other.roughness_texture);

        this->ao_texture_path = other.ao_texture_path;
        this->ao_texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity AO texture variant");
        }, other.ao_texture);

        this->surface_material_path = other.surface_material_path;
        this->surface_material = other.surface_material;
        this->collider = other.collider;
        this->visible = other.visible;
        this->isChild = other.isChild;
        this->isParent = other.isParent;
        this->running = other.running;
        this->running_first_time = other.running_first_time;
        this->calc_physics = other.calc_physics;
        this->isDynamic = other.isDynamic;
        this->mass = other.mass;
        this->inertia = other.inertia;
        this->id = other.id;
        this->parent = nullptr; 

        this->lodEnabled = other.lodEnabled;
        for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++)
            this->LodModels[i] = other.LodModels[i];

        this->children = other.children;
    }


    Entity& operator=(const Entity& other) {
        if (this == &other) {
            return *this;  // Handle self-assignment
        }

        this->initialized = other.initialized;
        this->name = other.name;
        this->size = other.size;
        this->position = other.position;
        this->rotation = other.rotation;
        this->scale = other.scale;
        this->relative_position = other.relative_position;
        this->relative_rotation = other.relative_rotation;
        this->relative_scale = other.relative_scale;
        this->script = other.script;
        this->script_index = other.script_index;
        this->model_path = other.model_path;
        this->ObjectType = other.ObjectType;
        this->model = other.model;
        this->bounds = other.bounds;
        this->texture_path = other.texture_path;
        this->tiling[0] = other.tiling[0];
        this->tiling[1] = other.tiling[1];
        this->lodEnabled = other.lodEnabled;


        for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++)
            this->LodModels[i] = other.LodModels[i];

        this->texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
        
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>) 
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity texture variant");
        }, other.texture);
    


        this->normal_texture_path = other.normal_texture_path;
        this->normal_texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity normal texture variant");
        }, other.normal_texture);

        this->roughness_texture_path = other.roughness_texture_path;
        this->roughness_texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity roughness texture variant");
        }, other.roughness_texture);

        this->ao_texture_path = other.ao_texture_path;
        this->ao_texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity ao texture variant");
        }, other.ao_texture);


        this->surface_material = other.surface_material;
        this->surface_material_path = other.surface_material_path;
        this->collider = other.collider;

        this->currentCollisionShapeType     = make_shared<CollisionShapeType>(*other.currentCollisionShapeType);
        if (other.boxRigidBody && *other.boxRigidBody != nullptr)
            this->boxRigidBody              = make_shared<btRigidBody *>(*other.boxRigidBody);
        else
            this->boxRigidBody              = make_shared<btRigidBody *>(nullptr);

        if (other.treeRigidBody && *other.treeRigidBody != nullptr)
            this->treeRigidBody             = make_shared<btRigidBody *>(*other.treeRigidBody);
        else
            this->treeRigidBody             = make_shared<btRigidBody *>(nullptr);

        this->visible = other.visible;
        this->isChild = other.isChild;
        this->isParent = other.isParent;
        this->running = other.running;
        this->running_first_time = other.running_first_time;
        this->calc_physics = other.calc_physics;
        this->isDynamic = other.isDynamic;
        this->mass = other.mass;
        this->inertia = other.inertia;
        this->id = other.id;
        this->parent = nullptr; // Avoid copying the parent-child relationship
        // You'll need to handle copying the vector of children properly
        // Note: Be sure to consider whether you need to deep copy the elements
        this->children = other.children; // Shallow copy of children


        return *this;
    }

    Entity(std::vector<Entity>& entities_list_pregame) {
        // Add the newly created Entity to the C++ vector
        entities_list_pregame.push_back(*this);
    }
    

    bool operator==(const Entity& other) const {
        return this->id == other.id;
    }

    void addInstance(Entity* instance) {
        instances.push_back(instance);


        if (transforms == nullptr) {
            transforms = (Matrix *)RL_CALLOC(instances.size(), sizeof(Matrix));
        } else {

            transforms = (Matrix *)RL_REALLOC(transforms, instances.size() * sizeof(Matrix));
        }


        int lastIndex = instances.size() - 1;
        calculateInstance(lastIndex);

        instancing_shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(instancing_shader, "mvp");
        instancing_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(instancing_shader, "viewPos");
        instancing_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(instancing_shader, "instanceTransform");

    }

    bool hasInstances()
    {
        return instances.empty();
    }

    void calculateInstance(int index) {
        if (index < 0 || index >= instances.size()) {

            return;
        }

        Entity* entity = instances.at(index);

        Matrix translation = MatrixTranslate(entity->position.x, entity->position.y, entity->position.z);
        Matrix rotation = MatrixRotateXYZ(Vector3{ DEG2RAD * entity->rotation.x, DEG2RAD * entity->rotation.y, DEG2RAD * entity->rotation.z });    

        transforms[index] = MatrixMultiply(rotation, translation);

        matInstances = LoadMaterialDefault();
    }
        
    void addChild(Entity& entityChild) {
        Entity* newChild = new Entity(entityChild);

        newChild->relative_position = {
            newChild->position.x - this->position.x,
            newChild->position.y - this->position.y,
            newChild->position.z - this->position.z
        };

        newChild->parent = this;
        children.push_back(newChild);
    }

    void addChild(Light* lightChild, int light_id) {
        lightChild->relative_position = {
            lightChild->position.x - this->position.x,
            lightChild->position.y - this->position.y,
            lightChild->position.z - this->position.z
        };

        auto it = std::find_if(lights_info.begin(), lights_info.end(), [light_id](const AdditionalLightInfo& light) {
            return light.id == light_id;
        });
        
        if (it != lights_info.end()) {
            AdditionalLightInfo* light_info = (AdditionalLightInfo*)&*it;
            light_info->parent = this;
            children.push_back(lightChild);
        }
    }

    void update_children()
    {
        if (children.empty()) return;
        
        for (std::variant<Entity*, Light*, Text*, LitButton*>& childVariant : children)
        {
            if (auto* child = std::get_if<Entity*>(&childVariant))
            {
                (*child)->render();

    #ifndef GAME_SHIPPING
                if (*child == selected_entity) continue;
    #endif

                (*child)->position = {this->position + (*child)->relative_position};
                (*child)->update_children();
            }
          
            else if (auto* child = std::get_if<Light*>(&childVariant))
            {                
                #ifndef GAME_SHIPPING
                    if (*child == selected_light && selected_game_object_type == "light") continue;
                #endif

                if (*child) {
                    (*child)->position = glm::vec3(this->position.x, this->position.y, this->position.z) + (*child)->relative_position;
                }
            }
        }
    }

    void makeChildrenInstances() {
        for (const auto& childVariant : children) {
            if (auto childEntity = std::get_if<Entity*>(&childVariant)) {
                addInstance(*childEntity); // Add child as an instance
                (*childEntity)->makeChildrenInstances(); // Recursively make children instances
            }
        }
    }


    void remove() {

        for (auto& childVariant : children) {
            if (auto* entity = std::get_if<Entity*>(&childVariant)) {
                delete *entity;
            } else if (auto* light = std::get_if<Light*>(&childVariant)) {
                delete *light;
            } else if (auto* text = std::get_if<Text*>(&childVariant)) {
                delete *text;
            } else if (auto* button = std::get_if<LitButton*>(&childVariant)) {
                delete *button;
            }
        }

        children.clear();

        entities_list_pregame.erase(
            std::remove_if(entities_list_pregame.begin(), entities_list_pregame.end(),
                [this](const Entity& entity) {
                    return entity.id == this->id;
                }),
            entities_list_pregame.end());

    }

    Color getColor() {
        return (Color) {
            static_cast<unsigned char>(surface_material.color.x * 255),
            static_cast<unsigned char>(surface_material.color.y * 255),
            static_cast<unsigned char>(surface_material.color.z * 255),
            static_cast<unsigned char>(surface_material.color.w * 255)
        };
    }

    void setColor(Color newColor) {
        surface_material.color = {
            newColor.r / 255,
            newColor.g / 255,
            newColor.b / 255,
            newColor.a / 255
        };
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
        if (entity_shader == nullptr)
            model.materials[0].shader = shader;
    }

    void loadModel(const char* filename, const char* textureFilename = NULL) {
        model = LoadModel(filename);
    }

    void ReloadTextures(bool force_reload = false) {
        if (!texture_path.empty() || !force_reload) {
            if (auto diffuse_texture = get_if<Texture2D>(&texture)) {
                model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *diffuse_texture;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *diffuse_texture;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&texture)) {
                (*videoPlayerPtr)->Update();
                model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = (*videoPlayerPtr)->GetTexture();
                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = (*videoPlayerPtr)->GetTexture();
                    }
                }
            }
        }

        if (!normal_texture_path.empty() || !force_reload) {
            if (auto normal = get_if<Texture2D>(&normal_texture)) {
                model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = *normal;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_NORMAL].texture = *normal;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&normal_texture)) {
                (*videoPlayerPtr)->Update();
                model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = (*videoPlayerPtr)->GetTexture();

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_NORMAL].texture = (*videoPlayerPtr)->GetTexture();
                    }
                }
            }
        }


        if (!roughness_texture_path.empty() || !force_reload) {
            if (auto roughness = get_if<Texture2D>(&roughness_texture)) {
                model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = *roughness;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = *roughness;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&roughness_texture)) {
                (*videoPlayerPtr)->Update();
                model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = (*videoPlayerPtr)->GetTexture();

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = (*videoPlayerPtr)->GetTexture();
                    }
                }
            }
        }

        if (!ao_texture_path.empty() || !force_reload) {
            if (auto ao = get_if<Texture2D>(&ao_texture)) {
                model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = *ao;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = *ao;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&ao_texture)) {
                (*videoPlayerPtr)->Update();
                model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = (*videoPlayerPtr)->GetTexture();

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = (*videoPlayerPtr)->GetTexture();
                    }
                }
            }
        }
    }

    void setModel(const char* modelPath = "", Model entity_model = Model(), Shader default_shader = shader)
    {
        model_path = modelPath;
    
        if (modelPath == "")
        {
            model = entity_model;
        } else
        {
            model = LoadModel(modelPath);
        }

        // lodEnabled = true;

        // this->LodModels[0] = this->model;

        // SimplifyMesh simplifier;
        // VertexIndices lodLevel1 = simplifier.simplify(this->model.meshes[0], 0.01f);
        // VertexIndices lodLevel2 = simplifier.simplify(this->model.meshes[0], 0.03f);
        // VertexIndices lodLevel3 = simplifier.simplify(this->model.meshes[0], 0.05f);
        
        // this->LodModels[1] = LoadModelFromMesh(GenerateLODMesh(lodLevel1, this->model.meshes[0]));
        // this->LodModels[2] = LoadModelFromMesh(GenerateLODMesh(lodLevel2, this->model.meshes[0]));
        // this->LodModels[3] = LoadModelFromMesh(GenerateLODMesh(lodLevel3, this->model.meshes[0]));

        if (isDynamic) {
            makePhysicsDynamic();
        } else {
            makePhysicsStatic();
        }

        ReloadTextures();
        
        setShader(default_shader);
    }

    bool hasModel()
    {
        return IsModelReady(model);
    }

    void setShader(Shader shader)
    {
        entity_shader = &shader;
        if (IsModelReady(model))
            model.materials[0].shader = shader;

        for (int index = 0; index < 4; index++)
            if (IsModelReady(LodModels[index]))
            LodModels[index].materials[0].shader = shader;
    }

    void runScript(std::reference_wrapper<Entity> entityRef, LitCamera* rendering_camera)
    {
        if (script.empty() && script_index.empty()) return;
        running = true;
        std::lock_guard<std::mutex> lock(script_mutex);
        
        py::gil_scoped_release release;
        py::gil_scoped_acquire acquire;

        if (!Entity_already_registered) {
            Entity_already_registered = true;
            py::class_<Entity>(entity_module, "Entity")
                .def(py::init<>([]() {
                    // Create an Entity instance
                    Entity* entity = new Entity();
                    entity->setColor(RAYWHITE);
                    entity->setScale(LitVector3{1,1,1});
                    entity->setName("New Entity");


                    // Add the newly created Entity to the vector
                    entities_list_pregame.push_back(*entity);

                    return entities_list_pregame.back(); // Return the Entity instance
                }))
                    
                .def_property("name",
                    &Entity::getName,
                    &Entity::setName)

                .def_property("position",
                    [](const Entity& entity) { return entity.position; },
                    [](Entity& entity, LitVector3& position) { entity.setPos(position); }
                )
                
                .def_readwrite("scale", &Entity::scale, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("rotation", &Entity::rotation, py::call_guard<py::gil_scoped_release>())
                .def_property("color",
                    &Entity::getColor, // Getter
                    &Entity::setColor, // Setter
                    py::call_guard<py::gil_scoped_release>())

                .def_readwrite("visible", &Entity::visible, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("id", &Entity::id, py::call_guard<py::gil_scoped_release>())
                .def_readwrite("collider", &Entity::collider, py::call_guard<py::gil_scoped_release>())
                .def("print_position", &Entity::print_position, py::call_guard<py::gil_scoped_release>())
                .def("applyForce", &Entity::applyForce, py::call_guard<py::gil_scoped_release>())
                .def("applyImpulse", &Entity::applyImpulse, py::call_guard<py::gil_scoped_release>())
                .def("makeStatic", &Entity::makePhysicsStatic, py::call_guard<py::gil_scoped_release>())
                .def("makeDynamic", &Entity::makePhysicsDynamic, py::call_guard<py::gil_scoped_release>());
        }



        Entity& this_entity = entityRef.get();
        py::object entity_obj = py::cast(&this_entity);

        py::module input_module = py::module::import("input_module");
        py::module collisions_module = py::module::import("collisions_module");
        py::module camera_module = py::module::import("camera_module");
        py::module time_module = py::module::import("time_module");
        py::module color_module = py::module::import("color_module");
        py::module math_module = py::module::import("math_module");
        
        py::dict locals = py::dict(
            "entity"_a = entity_obj,
            "IsMouseButtonPressed"_a = input_module.attr("IsMouseButtonPressed"),
            "IsKeyDown"_a = input_module.attr("IsKeyDown"),
            "IsKeyPressed"_a = input_module.attr("IsKeyPressed"),
            "IsKeyUp"_a = input_module.attr("IsKeyUp"),
            "GetMouseMovement"_a = input_module.attr("GetMouseMovement"),
            "KeyboardKey"_a = input_module.attr("KeyboardKey"),
            "MouseButton"_a = input_module.attr("MouseButton"),
            "raycast"_a = collisions_module.attr("raycast"),
            "Vector3"_a = math_module.attr("Vector3"),
            "Vector2"_a = math_module.attr("Vector2"),
            "Vector3Scale"_a = math_module.attr("Vector3Scale"),
            "Vector3Distance"_a = math_module.attr("Vector3Distance"),
            "Color"_a = color_module.attr("Color"),
            "time"_a = py::cast(&time_instance),
            "lerp"_a = math_module.attr("lerp"),
            "camera"_a = py::cast(rendering_camera)
        );

        locals["Entity"] = entity_module.attr("Entity");

        try {
            pybind11::gil_scoped_acquire acquire;

            string script_content;
#ifndef GAME_SHIPPING
            script_content = read_file_to_string(script);
#else
            auto it = scriptMap.find(script_index);

            if (it != scriptMap.end()) {
                script_content = it->second;
            } else {
                return; // Script not found
            }
#endif
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
                        rendering_camera->update();
                        update_func();
                        last_frame_count = time_instance.dt;
                    }
                }
            } else {
                std::cerr << "The 'update' function is not defined in the script.\n";
                return;
            }

            py::gil_scoped_release release;
        } catch (const py::error_already_set& e) {
            py::print(e.what());
        }
    }

    void calcPhysicsPosition() {
        if (!isDynamic) return;
        
        if (CollisionShapeType::Box == *currentCollisionShapeType) {
            btTransform trans;
            if (boxRigidBody && (*boxRigidBody)->getMotionState()) {
                (*boxRigidBody)->getMotionState()->getWorldTransform(trans);
                btVector3 rigidBodyPosition = trans.getOrigin();
                position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
            }
        } else if (*currentCollisionShapeType == CollisionShapeType::HighPolyMesh) {
            btTransform trans;
            if (treeRigidBody && (*treeRigidBody)->getMotionState()) {
                (*treeRigidBody)->getMotionState()->getWorldTransform(trans);
                btVector3 rigidBodyPosition = trans.getOrigin();
                position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
            }
        }
    }





    
    void calcPhysicsRotation() {
        if (!isDynamic) return;

        if (boxRigidBody) {
            btTransform trans;
            if ((*boxRigidBody)->getMotionState()) {
                (*boxRigidBody)->getMotionState()->getWorldTransform(trans);
                btQuaternion objectRotation = trans.getRotation();
                btScalar Roll, Yaw, Pitch;
                objectRotation.getEulerZYX(Roll, Yaw, Pitch);

                for (int index = 0; index < 4; index++)
                    LodModels[index].transform = MatrixRotateXYZ((Vector3){ Pitch, Yaw, Roll });
            }
        }
        else if (treeRigidBody) {
            btTransform trans;
            if ((*treeRigidBody)->getMotionState()) {
                (*treeRigidBody)->getMotionState()->getWorldTransform(trans);
                btQuaternion objectRotation = trans.getRotation();
                btScalar Roll, Yaw, Pitch;
                objectRotation.getEulerZYX(Roll, Yaw, Pitch);

                for (int index = 0; index < 4; index++)
                    LodModels[index].transform = MatrixRotateXYZ((Vector3){ Pitch, Yaw, Roll });
            }
        }
    }


    void setPos(LitVector3 newPos) {
        position = newPos;
        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(newPos.x, newPos.y, newPos.z));

            if ((*boxRigidBody)->getMotionState()) {
                (*boxRigidBody)->setWorldTransform(transform);
                (*boxRigidBody)->getMotionState()->setWorldTransform(transform);
            }
        }
    }

    void applyForce(const LitVector3& force) {
        if (boxRigidBody && isDynamic) {
            (*boxRigidBody)->setActivationState(ACTIVE_TAG);
            btVector3 btForce(force.x, force.y, force.z);
            (*boxRigidBody)->applyCentralForce(btForce);
        }
    }

    void applyImpulse(const LitVector3& impulse) {
        if (boxRigidBody && isDynamic) {
            (*boxRigidBody)->setActivationState(ACTIVE_TAG);
            btVector3 btImpulse(impulse.x, impulse.y, impulse.z);
            (*boxRigidBody)->applyCentralImpulse(btImpulse);
        }
    }

    void updateMass() {
        if (!isDynamic || dynamicBoxShape == nullptr) return;

        btScalar btMass = mass;
        btVector3 boxInertia(inertia.x, inertia.y, inertia.z);
        dynamicBoxShape->calculateLocalInertia(btMass, boxInertia);
        if (*currentCollisionShapeType == CollisionShapeType::Box && boxRigidBody && *boxRigidBody != nullptr)
            (*boxRigidBody)->setMassProps(btMass, boxInertia);
        else if (*currentCollisionShapeType == CollisionShapeType::HighPolyMesh && treeRigidBody && *treeRigidBody != nullptr)
            (*treeRigidBody)->setMassProps(btMass, boxInertia);

    }

    void createStaticBox(float x, float y, float z) {
        if (isDynamic) isDynamic = false;
        if (staticBoxShape == nullptr) {
            // Use the provided dimensions (x, y, z) to create the btBoxShape
            staticBoxShape = new btBoxShape(btVector3(x * scaleFactorRaylibBullet, y * scaleFactorRaylibBullet, z * scaleFactorRaylibBullet));

            // Remove existing rigid bodies
            if (boxRigidBody && *boxRigidBody != nullptr) {
                dynamicsWorld->removeRigidBody(*boxRigidBody);
                delete (*boxRigidBody)->getMotionState();
                delete *boxRigidBody;
                boxRigidBody = std::make_shared<btRigidBody*>(nullptr);
            }

            if (treeRigidBody && *treeRigidBody != nullptr) {
                dynamicsWorld->removeRigidBody(*treeRigidBody);
                delete (*treeRigidBody)->getMotionState();
                delete *treeRigidBody;
                treeRigidBody = std::make_shared<btRigidBody*>(nullptr);
            }

            dynamicBoxShape = nullptr;

            // Set up the transformation
            btTransform groundTransform;
            groundTransform.setIdentity();

            // Use radians for rotation
            float rollRad = glm::radians(rotation.x);
            float pitchRad = glm::radians(rotation.y);
            float yawRad = glm::radians(rotation.z);

            btQuaternion quaternion;
            quaternion.setEulerZYX(yawRad, pitchRad, rollRad);

            groundTransform.setRotation(quaternion);
            groundTransform.setOrigin(btVector3(position.x, position.y, position.z));

            // Create the motion state and rigid body construction info
            btDefaultMotionState* groundMotionState = new btDefaultMotionState(groundTransform);
            btRigidBody::btRigidBodyConstructionInfo groundRigidBodyCI(0, groundMotionState, staticBoxShape, btVector3(0, 0, 0));

            // Create the rigid body
            btRigidBody* groundRigidBody = new btRigidBody(groundRigidBodyCI);
            boxRigidBody = std::make_shared<btRigidBody*>(groundRigidBody);

            // Add the rigid body to the dynamics world
            dynamicsWorld->addRigidBody(*boxRigidBody);
            currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::Box);
        }
    }

    void createStaticMesh() {
        if (isDynamic)
            isDynamic = true;

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);

        if (treeRigidBody != nullptr && *treeRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*treeRigidBody);
            delete (*treeRigidBody)->getMotionState();
            delete *treeRigidBody;
            treeRigidBody = nullptr;
        }
        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
            delete (*boxRigidBody)->getMotionState();
            delete *boxRigidBody;
            boxRigidBody= nullptr;
        }


        if (staticBoxShape) {
            delete staticBoxShape;
            boxMotionState = nullptr;
            staticBoxShape = nullptr;
            dynamicBoxShape = nullptr;
        }

        // Create a btBvhTriangleMeshShape for the ground
        btTriangleMesh* triangleMesh = new btTriangleMesh();

        for (int m = 0; m < model.meshCount; m++) {
            Mesh mesh = model.meshes[m];
            float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

            for (int v = 0; v < mesh.vertexCount; v += 9) {
                triangleMesh->addTriangle(
                    btVector3(meshVertices[v], meshVertices[v + 1], meshVertices[v + 2]),
                    btVector3(meshVertices[v + 3], meshVertices[v + 4], meshVertices[v + 5]),
                    btVector3(meshVertices[v + 6], meshVertices[v + 7], meshVertices[v + 8])
                );
            }
        }

        btBvhTriangleMeshShape* groundShape = new btBvhTriangleMeshShape(triangleMesh, true);

        // Create the rigid body for the ground
        btTransform groundTransform;
        groundTransform.setIdentity();
        groundTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btDefaultMotionState* groundMotionState = new btDefaultMotionState(groundTransform);
        btRigidBody::btRigidBodyConstructionInfo groundRigidBodyCI(0, groundMotionState, groundShape, btVector3(position.x, position.y, position.z));
        btRigidBody* groundRigidBody = new btRigidBody(groundRigidBodyCI);


        Matrix scaleMatrix = MatrixScale(model.transform.m0, model.transform.m5, model.transform.m10);
        float scaleX = scaleMatrix.m0 * scale.x;
        float scaleY = scaleMatrix.m5 * scale.y;
        float scaleZ = scaleMatrix.m10 * scale.z;


        btVector3 scaleVector(scaleX * scaleFactorRaylibBullet, scaleY * scaleFactorRaylibBullet, scaleZ * scaleFactorRaylibBullet);
        groundRigidBody->getCollisionShape()->setLocalScaling(scaleVector);

        // Add the ground rigid body to the dynamics world
        dynamicsWorld->addRigidBody(groundRigidBody);
    }


    void createDynamicBox(float x, float y, float z) {
        if (!isDynamic) isDynamic = true;

        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
            delete *boxRigidBody;
            boxRigidBody = make_shared<btRigidBody*>(nullptr);
        }

        if (treeRigidBody && *treeRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*treeRigidBody);
            delete *treeRigidBody;
            treeRigidBody = make_shared<btRigidBody*>(nullptr);
        }

        dynamicBoxShape = nullptr;
        boxMotionState = nullptr;

        dynamicBoxShape = new btBoxShape(btVector3(x * scaleFactorRaylibBullet, y * scaleFactorRaylibBullet, z * scaleFactorRaylibBullet));
        currentCollisionShapeType = make_shared<CollisionShapeType>(CollisionShapeType::Box);

        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btScalar btMass = mass;

        btVector3 localInertia(inertia.x, inertia.y, inertia.z);

        dynamicBoxShape->calculateLocalInertia(btMass, localInertia);

        boxMotionState = new btDefaultMotionState(startTransform);
        btRigidBody::btRigidBodyConstructionInfo boxRigidBodyCI(btMass, boxMotionState, dynamicBoxShape, localInertia);
        btRigidBody* boxRigidBodyPtr = new btRigidBody(boxRigidBodyCI);
        boxRigidBody = std::make_shared<btRigidBody*>(boxRigidBodyPtr);

        dynamicsWorld->addRigidBody(*boxRigidBody);
    }

    void createDynamicMesh() {
        if (!isDynamic) isDynamic = true;

        currentCollisionShapeType = make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);

        if (treeRigidBody != nullptr && *treeRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*treeRigidBody);
            delete (*treeRigidBody)->getMotionState();
            delete *treeRigidBody;
            treeRigidBody = nullptr;
        }
        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
            delete (*boxRigidBody)->getMotionState();
            delete *boxRigidBody;
            boxRigidBody= nullptr;
        }

        if (staticBoxShape) {
            boxMotionState = nullptr;
            staticBoxShape = nullptr;
            dynamicBoxShape = nullptr;
        }

        customMeshShape = new btConvexHullShape();

        for (int m = 0; m < model.meshCount; m++) {
            Mesh mesh = model.meshes[m];
            float* meshVertices = (float*)mesh.vertices;

            for (int v = 0; v < mesh.vertexCount; v += 3) {
                customMeshShape->addPoint(btVector3(meshVertices[v], meshVertices[v + 1], meshVertices[v + 2]));
            }
        }

        // Set up the dynamics of your tree object
        btTransform treeTransform;
        treeTransform.setIdentity();
        treeTransform.setOrigin(btVector3(0, 10, 0));

        btScalar treeMass = 1.0f;
        btVector3 treeInertia(0, 0, 0);
        customMeshShape->calculateLocalInertia(treeMass, treeInertia);
        btDefaultMotionState* treeMotionState = new btDefaultMotionState(treeTransform);
        btRigidBody::btRigidBodyConstructionInfo treeRigidBodyCI(treeMass, treeMotionState, customMeshShape, treeInertia);
        btRigidBody* treeRigidBodyPtr = new btRigidBody(treeRigidBodyCI);
        treeRigidBody = std::make_shared<btRigidBody*>(treeRigidBodyPtr);

        dynamicsWorld->addRigidBody(*treeRigidBody);
    }

    void makePhysicsDynamic(CollisionShapeType shapeType = CollisionShapeType::Box) {
        isDynamic = true;

        if (shapeType == CollisionShapeType::Box)
            createDynamicBox(scale.x, scale.y, scale.z);
        else if (shapeType == CollisionShapeType::HighPolyMesh)
            createDynamicMesh();
    }

    void makePhysicsStatic(CollisionShapeType shapeType = CollisionShapeType::None) {
        if (shapeType == None) {
            shapeType = *currentCollisionShapeType;
        } 
 
        isDynamic = false;
 
        if (shapeType == CollisionShapeType::Box)
            createStaticBox(scale.x, scale.y, scale.z);
        else if (shapeType == CollisionShapeType::HighPolyMesh)
            createStaticMesh();

    }

    void reloadRigidBody() {
        if (isDynamic)
            makePhysicsDynamic(*currentCollisionShapeType);
        else
            makePhysicsStatic();
    }

    void resetPhysics() {
        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            (*boxRigidBody)->setLinearVelocity(btVector3(0, 0, 0));
            (*boxRigidBody)->setAngularVelocity(btVector3(0, 0, 0));

            setPos(backupPosition);
        }
    }

    void print_position()
    {
        std::cout << "Position: " << position.x << ", " << position.y << ", " << position.z << "\n";
    }


    bool inFrustum()
    {
        UpdateFrustum();
        return AABBoxInFrustum(bounds.min, bounds.max);
    }




    void render() {
        if (!hasModel())
            initializeDefaultModel();

        update_children();

        if (calc_physics)
        {
            if (*currentCollisionShapeType != CollisionShapeType::Box && *currentCollisionShapeType != CollisionShapeType::HighPolyMesh && !isDynamic)
                makePhysicsStatic();
            else if (*currentCollisionShapeType != CollisionShapeType::None && isDynamic)
                calcPhysicsPosition();
        }
        else
        {
            setPos(position);    
            updateMass();
            backupPosition = position;
        }

        if (!visible) {
            return;
        }

        SetShaderValue(shader, GetShaderLocation(shader, "tiling"), tiling, SHADER_UNIFORM_VEC2);


        Matrix rotationMat = MatrixRotateXYZ((Vector3){
            DEG2RAD * rotation.x,
            DEG2RAD * rotation.y,
            DEG2RAD * rotation.z
        });

        Matrix scaleMat = MatrixScale(scale.x, scale.y, scale.z);
        model.transform = MatrixMultiply(scaleMat, rotationMat);
        
        if (!instances.empty())
        {
            PassSurfaceMaterials();

            glUseProgram((GLuint)instancing_shader.id);

            bool normalMapInit = !normal_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)instancing_shader.id, "normalMapInit"), normalMapInit);

            bool roughnessMapInit = !roughness_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)instancing_shader.id, "roughnessMapInit"), roughnessMapInit);

            matInstances = LoadMaterialDefault();

            instancing_shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(instancing_shader, "mvp");
            instancing_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(instancing_shader, "viewPos");
            instancing_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(instancing_shader, "instanceTransform");

            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = {
                static_cast<unsigned char>(surface_material.color.x * 255),
                static_cast<unsigned char>(surface_material.color.y * 255),
                static_cast<unsigned char>(surface_material.color.z * 255),
                static_cast<unsigned char>(surface_material.color.w * 255)
            };

            DrawMeshInstanced(model.meshes[0], model.materials[0], transforms, instances.size());
        }
        else
        {

            if (hasModel()) {
                if (!inFrustum()) {
                    return; // Early return if not in the frustum
                }
            }
            
            if (hasModel())
            {
                Matrix transformMatrix = MatrixIdentity();
                transformMatrix = MatrixScale(scale.x, scale.y, scale.z);
                transformMatrix = MatrixMultiply(transformMatrix, MatrixRotateXYZ(rotation));
                transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(position.x, position.y, position.z));

                if (model.meshes != nullptr)
                    bounds = GetMeshBoundingBox(model.meshes[0]);
                
                bounds.min = Vector3Transform(bounds.min, transformMatrix);
                bounds.max = Vector3Transform(bounds.max, transformMatrix);
            }


            PassSurfaceMaterials();
            
            ReloadTextures();
            glUseProgram((GLuint)shader.id);

            bool normalMapInit = !normal_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)shader.id, "normalMapInit"), normalMapInit);

            bool roughnessMapInit = !roughness_texture_path.empty();
            glUniform1i(glGetUniformLocation((GLuint)shader.id, "roughnessMapInit"), roughnessMapInit);

            float distance;

#ifndef GAME_SHIPPING
            if (in_game_preview)
                distance = Vector3Distance(this->position, camera.position);
            else
                distance = Vector3Distance(this->position, scene_camera.position);
#else
            distance = Vector3Distance(this->position, camera.position);
#endif

            int lodLevel = 0;

            if (distance < LOD_DISTANCE_HIGH) {
                lodLevel = 0;
            } else if (distance < LOD_DISTANCE_MEDIUM) {
                lodLevel = 1;
            } else if (distance < LOD_DISTANCE_LOW) {
                lodLevel = 2;
            } else {
                lodLevel = 3;
            }

            for (Model& lodModel : LodModels)
                lodModel.transform = MatrixMultiply(scaleMat, rotationMat);

            if (lodEnabled && IsModelReady(LodModels[lodLevel]))
            {
                DrawModel(
                    LodModels[lodLevel],
                    position, 
                    1,
                    (Color) {
                        static_cast<unsigned char>(surface_material.color.x * 255),
                        static_cast<unsigned char>(surface_material.color.y * 255),
                        static_cast<unsigned char>(surface_material.color.z * 255),
                        static_cast<unsigned char>(surface_material.color.w * 255)
                    }
                );
            }
            else
            {

                DrawModel(
                    model,
                    position, 
                    1, 
                    (Color) {
                        static_cast<unsigned char>(surface_material.color.x * 255),
                        static_cast<unsigned char>(surface_material.color.y * 255),
                        static_cast<unsigned char>(surface_material.color.z * 255),
                        static_cast<unsigned char>(surface_material.color.w * 255)
                    }
                );
            }
            
        }

    }

private:
    void PassSurfaceMaterials()
    {
        glGenBuffers(1, &surface_material_ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, surface_material_ubo);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(SurfaceMaterial), &this->surface_material, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        
        GLuint bindingPoint = 0;
        glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, surface_material_ubo);


        
        glBindBuffer(GL_UNIFORM_BUFFER, surface_material_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SurfaceMaterial), &this->surface_material);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

    }
};


bool operator==(const Entity& e, const Entity* ptr) {
    return &e == ptr;
}

#ifndef GAME_SHIPPING
    void AddEntity(
        bool create_immediatly = false,
        bool is_child = false,
        const char* model_path = "",
        Model model = LoadModelFromMesh(GenMeshCube(1,1,1)),
        string name = "Unnamed Entity"
    )
    {
        const int POPUP_WIDTH = 600;
        const int POPUP_HEIGHT = 650;

        int popupX = GetScreenWidth() / 4.5;
        int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

        if (create || create_immediatly)
        {
            Color entity_color_raylib = {
                static_cast<unsigned char>(entity_create_color.x * 255),
                static_cast<unsigned char>(entity_create_color.y * 255),
                static_cast<unsigned char>(entity_create_color.z * 255),
                static_cast<unsigned char>(entity_create_color.w * 255)
            };

            Entity entity_create;
            entity_create.setColor(entity_color_raylib);
            entity_create.setScale(Vector3{entity_create_scale, entity_create_scale, entity_create_scale});
            entity_create.setName(name);
            entity_create.isChild = is_create_entity_a_child || is_child;
            entity_create.setModel(model_path, model);
            entity_create.setShader(shader);

            if (!entities_list_pregame.empty())
            {
                int id = entities_list_pregame.back().id + 1;
                entity_create.id = id;
            }
            else
                entity_create.id = 0;

            if (!is_create_entity_a_child)
            {
                entities_list_pregame.reserve(1);
                entities_list_pregame.emplace_back(entity_create);
            }
            else
            {
                if (selected_game_object_type == "entity")
                {
                    if (selected_entity->isChild)
                        selected_entity->addChild(entity_create);
                    else
                        entities_list_pregame.back().addChild(entity_create);
                }
            }

            selected_entity = &entity_create;

            int last_entity_index = entities_list_pregame.size() - 1;
            listViewExActive = last_entity_index;

            create = false;
            is_create_entity_a_child = false;
            canAddEntity = false;
        }
        else if (canAddEntity)
        {
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.6f, 0.6f, 0.6f, 0.6f)); 

            ImGui::Begin("Entities");

            ImVec2 size = ImGui::GetContentRegionAvail();

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 50.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

            ImGui::SetCursorPosX(size.x / 2 - 250);
            ImGui::SetCursorPosY(size.y / 4);
            ImGui::Button("Entity Add Menu", ImVec2(500,100));

            ImGui::PopStyleColor(4);

            /* Scale Slider */
            ImGui::Text("Scale: ");
            ImGui::SameLine();
            ImGui::SliderFloat(" ", &entity_create_scale, 0.1f, 100.0f);

            /* Name Input */
            ImGui::InputText("##text_input_box", (char*)name.c_str(), sizeof(name));
            
            /* Color Picker */
            ImGui::Text("Color: ");
            ImGui::SameLine();
            ImGui::ColorEdit4(" ", (float*)&entity_create_color, ImGuiColorEditFlags_NoInputs);

            /* Is Children */
            ImGui::Checkbox("Is Children: ", &is_create_entity_a_child);

            /* Create BTN */
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14, 0.37, 0.15, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18, 0.48, 0.19, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

            ImGui::SetCursorPosX(size.x / 2);
            ImGui::SetCursorPosY(size.y / 1.1);
            bool create_entity_btn = ImGui::Button("Create", ImVec2(200,50));
            if (create_entity_btn)
            {
                canAddEntity = false;
                create = true;
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            ImGui::End();
        }
    }
#endif
    

void updateEntitiesList(std::vector<Entity>& entities_list, const std::vector<Entity>& entities_list_pregame) {
    // std::unordered_map<int, Entity> entityMap;

    // // Create a map of entity IDs to entities in entities_list
    // for (const Entity& entity : entities_list) {
    //     entityMap[entity.id] = entity;
    // }

    // // Update entities_list by adding new entities and removing entities not in entities_list_pregame
    // for (const Entity& pregameEntity : entities_list_pregame) {
    //     entityMap[pregameEntity.id] = pregameEntity;
    // }

    // // Clear entities_list and insert entities from the map
    // entities_list.clear();
    // for (const auto& pair : entityMap) {
    //     entities_list.push_back(pair.second);
    // }
}

HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore)
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

    float minDistance = 1000000000000000000000000000000000.0f;

    for (int index = 0; index < entities_list.size(); index++)
    {
        Entity& entity = entities_list[index];

        if (std::find(ignore.begin(), ignore.end(), entity) != ignore.end())
            continue;

        if (!entity.collider)
            continue;

        float extreme_rotation = GetExtremeValue(entity.rotation);

        Matrix matScale = MatrixScale(entity.scale.x, entity.scale.y, entity.scale.z);
        Matrix matRotation = MatrixRotate(entity.rotation, extreme_rotation * DEG2RAD);
        Matrix matTranslation = MatrixTranslate(entity.position.x, entity.position.y, entity.position.z);

        Matrix modelMatrix = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);
        RayCollision meshHitInfo = { 0 };

        for (int mesh_i = 0; mesh_i < entity.model.meshCount; mesh_i++)
        {
            meshHitInfo = GetRayCollisionMesh(ray, entity.model.meshes[mesh_i], modelMatrix);
            if (meshHitInfo.hit)
            {
                _hitInfo.hit = true;
                _hitInfo.distance = meshHitInfo.distance;
                _hitInfo.entity = std::make_shared<Entity>(entity);
                _hitInfo.worldPoint = meshHitInfo.point;
                _hitInfo.worldNormal = meshHitInfo.normal;
                _hitInfo.hitColor = {
                    static_cast<unsigned char>(entity.surface_material.color.x * 255),
                    static_cast<unsigned char>(entity.surface_material.color.w * 255),
                    static_cast<unsigned char>(entity.surface_material.color.y * 255),
                    static_cast<unsigned char>(entity.surface_material.color.z * 255)
                };

                minDistance = meshHitInfo.distance;
            }
        }
    }

    pybind11::gil_scoped_release release;

    return _hitInfo;
}
