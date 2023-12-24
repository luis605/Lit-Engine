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




const char* encryptFileString(const std::string& inputFile, const std::string& key) {
    std::ifstream inFile(inputFile, std::ios::binary);

    if (!inFile) {
        std::cerr << "Failed to open encrypted file." << std::endl;
        return nullptr;
    }

    size_t keyLength = key.size();
    size_t bufferSize = 4096;
    char buffer[4096];

    size_t bytesRead = 0;
    size_t keyIndex = 0;
    std::string encryptedData;

    while (inFile.good()) {
        inFile.read(buffer, bufferSize);
        bytesRead = inFile.gcount();

        for (size_t i = 0; i < bytesRead; ++i) {
            buffer[i] ^= key[keyIndex++];
            keyIndex %= keyLength;
        }

        encryptedData.append(buffer, bytesRead);
    }

    inFile.close();
    
    char* encryptedCString = new char[encryptedData.size() + 1];
    std::strcpy(encryptedCString, encryptedData.c_str());


    return encryptedCString;
}


const char* decryptFileString(const std::string& inputFile, const std::string& key) {
    return encryptFileString(inputFile, key); 
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


py::module entity_module("entity_module");


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


// thread_local std::mutex script_mutex;
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
    btConvexHullShape* triangleMesh                = nullptr;
    std::shared_ptr<btRigidBody*>(boxRigidBody)    = make_shared<btRigidBody*>(nullptr);
    std::shared_ptr<btRigidBody*>(highPolyDynamicRigidBody)   = make_shared<btRigidBody*>(nullptr);
    LitVector3 backupPosition                      = position;
    vector<Entity*> instances;
    Matrix *transforms                             = nullptr;
    Material matInstances;
    int lastIndexCalculated                        = -1;
    Shader* entity_shader;
    bool lodEnabled                                = true;

    py::object entity_obj;
    string script_content;
    py::dict locals;
    py::module script_module;

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

        if (other.highPolyDynamicRigidBody && *other.highPolyDynamicRigidBody != nullptr)
            this->highPolyDynamicRigidBody             = make_shared<btRigidBody *>(*other.highPolyDynamicRigidBody);
        else
            this->highPolyDynamicRigidBody             = make_shared<btRigidBody *>(nullptr);

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

        this->script_content = other.script_content;

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
        this->script_content = other.script_content;


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

        if (other.highPolyDynamicRigidBody && *other.highPolyDynamicRigidBody != nullptr)
            this->highPolyDynamicRigidBody             = make_shared<btRigidBody *>(*other.highPolyDynamicRigidBody);
        else
            this->highPolyDynamicRigidBody             = make_shared<btRigidBody *>(nullptr);

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
        this->children = other.children;


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
        // Delete objects in children vector
        for (auto& childVariant : children) {
            std::visit([](auto& child) { delete child; }, childVariant);
        }

        // Clear the children vector
        children.clear();

        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
            delete (*boxRigidBody)->getMotionState();
            delete *boxRigidBody;
            boxRigidBody = std::make_shared<btRigidBody*>(nullptr);
        }

        if (highPolyDynamicRigidBody && *highPolyDynamicRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*highPolyDynamicRigidBody);
            delete (*highPolyDynamicRigidBody)->getMotionState();
            delete *highPolyDynamicRigidBody;
            highPolyDynamicRigidBody = std::make_shared<btRigidBody*>(nullptr);
        }

        // Remove the corresponding entity from entities_list_pregame
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



        std::vector<uint32_t> indices;
        std::vector<Vector3> vertices;

        for (size_t i = 0; i < model.meshes[0].vertexCount; ++i) {
            size_t baseIndex = i * 3;
            float x = model.meshes[0].vertices[baseIndex];
            float y = model.meshes[0].vertices[baseIndex + 1];
            float z = model.meshes[0].vertices[baseIndex + 2];

            size_t ix;
            if (model.meshes[0].indices)
                ix = model.meshes[0].indices[i];
            else
                ix = i;

            vertices.push_back({x, y, z});
            indices.push_back(ix);
        }

        if (vertices.size() > 150)
        {
            lodEnabled = true;

            OptimizedMeshData data(indices, vertices);

            this->LodModels[0] = this->model;

            data = OptimizeMesh(model.meshes[0], indices, vertices, 0.05);
            this->LodModels[1] = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, data.vertexCount, model.meshes[0]));

            data = OptimizeMesh(model.meshes[0], indices, vertices, 0.6);
            this->LodModels[2] = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, data.vertexCount, model.meshes[0]));

            data = OptimizeMesh(model.meshes[0], indices, vertices, 1.0);
            this->LodModels[3] = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, data.vertexCount, model.meshes[0]));
        }        

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

    void setupScript(LitCamera* rendering_camera)
    {
        if (script.empty() && script_index.empty()) return;
        running = true;


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
                
                .def_readwrite("scale", &Entity::scale)
                .def_readwrite("rotation", &Entity::rotation)
                .def_property("color",
                    &Entity::getColor, // Getter
                    &Entity::setColor // Setter
                    )

                .def_readwrite("visible", &Entity::visible)
                .def_readwrite("id", &Entity::id)
                .def_readwrite("collider", &Entity::collider)
                .def("print_position", &Entity::print_position)
                .def("applyForce", &Entity::applyForce)
                .def("applyImpulse", &Entity::applyImpulse)
                .def("makeStatic", &Entity::makePhysicsStatic)
                .def("makeDynamic", &Entity::makePhysicsDynamic);
        }


        py::module input_module = py::module::import("input_module");
        py::module collisions_module = py::module::import("collisions_module");
        py::module camera_module = py::module::import("camera_module");
        py::module time_module = py::module::import("time_module");
        py::module color_module = py::module::import("color_module");
        py::module math_module = py::module::import("math_module");

        entity_obj = py::cast(this);


        locals = py::dict(
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



#ifndef GAME_SHIPPING
        script_content = read_file_to_string(script);
#else
    std::ifstream infile("encryptedScripts.json");
    if (!infile.is_open()) {
        std::cout << "Error: Failed to open scripts file." << std::endl;
        return;
    }

    const char* decryptedScripts = decryptFileString("encryptedScripts.json", "141b5aceaaa5582ec3efb9a17cac2da5e52bbc1057f776e99a56a064f5ea40d5f8689b7542c4d0e9d6d7163b9dee7725369742a54905ac95c74be5cb1435fdb726fead2437675eaa13bc77ced8fb9cc6108d4a247a2b37b76a6e0bf41916fcc98ee5f85db11ecb52b0d94b5fbab58b1f4814ed49e761a7fb9dfb0960f00ecf8c87989b8e92a630680128688fa7606994e3be12734868716f9df27674700a2cb37440afe131e570a4ee9e7e867aab18a44ee972956b7bd728f9b937c973b9726f6bdd56090d720e6fa31c70b31e0216739cde4210bcd93671c1e8edb752b32f782b62eab4d77a51e228a6b6ac185d7639bd037f9195c3f05c5d2198947621814827f2d99dd7c2821e76635a845203f42060e5a9a494482afab1c42c23ba5f317f250321c7713c2ce19fe7a3957ce439f4782dbee3d418aebe08314a4d6ac7b3d987696d39600c5777f555a8dc99f2953ab45b0687efa1a77d8e5b448b37a137f2849c9b76fec98765523869c22a3453c214ec8e8827acdded27c37d96017fbf862a405b4b06fe0e815e09ed5288ccd9139e67c7feed3e7306f621976b9d3ba917d19ef4a13490f9e2af925996f59a87uihjoklas9emyuikw75igeturf7unftyngl635n4554hs23d2453pfds");

    json json_data;
    try {
        // Parse the decrypted string into a JSON object
        json_data = json::parse(decryptedScripts);
    } catch (const json::parse_error& e) {
        return;
    }

    infile.close();

    if (json_data.is_array() && !json_data.empty()) {
        for (const auto& element : json_data) {

            if (element.is_object()) {
                if (element.contains(script_index)) {
                    script_content = element[script_index].get<std::string>();
                    std::cout << "Script loaded successfully." << std::endl;
                } else {
                }
            } else {
            }
        }
    } else {
        return;
    }

#endif

        try {
            script_module = py::module("__main__");

            for (auto item : locals) {
                script_module.attr(item.first) = item.second;
            }
            
            std::string script_content_copy = script_content;
            py::eval<py::eval_statements>(script_content_copy, script_module.attr("__dict__"));
        } catch (const py::error_already_set& e) {
            py::print(e.what());
        }

    }


    void runScript(LitCamera* rendering_camera) {
        if (script.empty() && script_index.empty()) {
            return;
        }

        try {
            if (py::hasattr(script_module, "update")) {
                py::object update_func = script_module.attr("update");
                locals["time"] = py::cast(&time_instance);
                update_func();
                rendering_camera->update();
            }
        } catch (const py::error_already_set& e) {
            std::cerr << "Error running script: " << e.what() << std::endl;
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
            if (highPolyDynamicRigidBody && (*highPolyDynamicRigidBody)->getMotionState()) {
                (*highPolyDynamicRigidBody)->getMotionState()->getWorldTransform(trans);
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
        else if (highPolyDynamicRigidBody) {
            btTransform trans;
            if ((*highPolyDynamicRigidBody)->getMotionState()) {
                (*highPolyDynamicRigidBody)->getMotionState()->getWorldTransform(trans);
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

            if ((*boxRigidBody)->getMotionState() ) {
                (*boxRigidBody)->setWorldTransform(transform);
                if ((*boxRigidBody)->getMotionState())
                    (*boxRigidBody)->getMotionState()->setWorldTransform(transform);
            }
        }
    }

    void setRot(LitVector3 newRot) {
        rotation = newRot;

        if (CollisionShapeType::Box == *currentCollisionShapeType) {
            if (boxRigidBody && *boxRigidBody && (*boxRigidBody)->getMotionState()) {
                btTransform currentTransform = (*boxRigidBody)->getWorldTransform();

                // Set the new rotation (in this example, a 90-degree rotation around the Y-axis)
                btQuaternion newRotation;
                newRotation.setEulerZYX(newRot.z * DEG2RAD, newRot.y * DEG2RAD, newRot.x * DEG2RAD);

                // Apply the new rotation to the transform
                currentTransform.setRotation(newRotation);

                // Set the updated transform to the rigid body
                (*boxRigidBody)->setWorldTransform(currentTransform);
            }
            // You may want to add an else block here to handle the case where boxRigidBody or its motion state is null.
        }
    }



    void setScale(Vector3 newScale) {
        scale = newScale;

        if (CollisionShapeType::Box == *currentCollisionShapeType) {
            if (isDynamic)
                createDynamicBox(scale.x, scale.y, scale.z);
            else
                createStaticBox(scale.x, scale.y, scale.z);
        }

        else if (CollisionShapeType::HighPolyMesh == *currentCollisionShapeType) {
            if (isDynamic)
                createDynamicMesh(false);
            else
                createStaticMesh(false);
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
        else if (*currentCollisionShapeType == CollisionShapeType::HighPolyMesh && highPolyDynamicRigidBody && *highPolyDynamicRigidBody != nullptr)
            (*highPolyDynamicRigidBody)->setMassProps(btMass, boxInertia);

    }

    void createStaticBox(float x, float y, float z) {
        if (isDynamic) isDynamic = false;

        staticBoxShape = new btBoxShape(btVector3(x * scaleFactorRaylibBullet, y * scaleFactorRaylibBullet, z * scaleFactorRaylibBullet));

        if (boxRigidBody && *boxRigidBody != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
            boxRigidBody = std::make_shared<btRigidBody*>(nullptr);
        }

        if (highPolyDynamicRigidBody && *highPolyDynamicRigidBody != nullptr) {
            dynamicsWorld->removeRigidBody(*highPolyDynamicRigidBody);
            highPolyDynamicRigidBody = std::make_shared<btRigidBody*>(nullptr);
        }

        dynamicBoxShape = nullptr;

        btTransform groundTransform;
        groundTransform.setIdentity();

        float rollRad = glm::radians(rotation.x);
        float pitchRad = glm::radians(rotation.y);
        float yawRad = glm::radians(rotation.z);

        btQuaternion quaternion;
        quaternion.setEulerZYX(yawRad, pitchRad, rollRad);

        groundTransform.setRotation(quaternion);
        groundTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btDefaultMotionState* groundMotionState = new btDefaultMotionState(groundTransform);
        btRigidBody::btRigidBodyConstructionInfo highPolyStaticRigidBodyCI(0, groundMotionState, staticBoxShape, btVector3(0, 0, 0));

        btRigidBody* highPolyStaticRigidBody = new btRigidBody(highPolyStaticRigidBodyCI);
        boxRigidBody = std::make_shared<btRigidBody*>(highPolyStaticRigidBody);

        dynamicsWorld->addRigidBody(*boxRigidBody);

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::Box);
    }

    void createStaticMesh(bool generateShape = true) {
        if (!isDynamic) isDynamic = true;

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);

        if (highPolyDynamicRigidBody != nullptr && *highPolyDynamicRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*highPolyDynamicRigidBody);
        }
        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
        }

        if (generateShape || !customMeshShape) {
            customMeshShape = new btConvexHullShape();

            for (int m = 0; m < model.meshCount; m++) {
                Mesh mesh = model.meshes[m];
                float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

                for (int v = 0; v < mesh.vertexCount; v += 3) {
                    btVector3 scaledVertex(
                        meshVertices[v]     * scale.x, 
                        meshVertices[v + 1] * scale.y,
                        meshVertices[v + 2] * scale.z
                    );
                    customMeshShape->addPoint(scaledVertex);
                }
            }
        }

        // Set up the dynamics of your tree object
        btTransform treeTransform;
        treeTransform.setIdentity();
        treeTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btDefaultMotionState* groundMotionState = new btDefaultMotionState(treeTransform);

        btScalar treeMass = 0.0f;
        btVector3 treeInertia(0, 0, 0);
        customMeshShape->calculateLocalInertia(treeMass, treeInertia);
        btDefaultMotionState* treeMotionState = new btDefaultMotionState(treeTransform);
        btRigidBody::btRigidBodyConstructionInfo highPolyDynamicRigidBodyCI(treeMass, treeMotionState, customMeshShape, treeInertia);
        btRigidBody* highPolyDynamicRigidBodyPtr = new btRigidBody(highPolyDynamicRigidBodyCI);
        highPolyDynamicRigidBody = std::make_shared<btRigidBody*>(highPolyDynamicRigidBodyPtr);

        dynamicsWorld->addRigidBody(*highPolyDynamicRigidBody);
    }


    void createDynamicBox(float x, float y, float z) {
        isDynamic = true;

        if (boxRigidBody && *boxRigidBody != nullptr && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
            boxRigidBody = make_shared<btRigidBody*>(nullptr);
        }

        if (highPolyDynamicRigidBody && *highPolyDynamicRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*highPolyDynamicRigidBody);
            highPolyDynamicRigidBody = make_shared<btRigidBody*>(nullptr);
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

    void createDynamicMesh(bool generateShape = true) {
        isDynamic = false;

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);

        if (highPolyDynamicRigidBody != nullptr && *highPolyDynamicRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*highPolyDynamicRigidBody);
        }
        if (boxRigidBody && *boxRigidBody.get() != nullptr) {
            dynamicsWorld->removeRigidBody(*boxRigidBody);
        }

        if (generateShape || !customMeshShape) {
            customMeshShape = new btConvexHullShape();

            for (int m = 0; m < model.meshCount; m++) {
                Mesh mesh = model.meshes[m];
                float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

                for (int v = 0; v < mesh.vertexCount; v += 3) {
                    // Apply scaling to the vertex coordinates
                    btVector3 scaledVertex(meshVertices[v] * scale.x, meshVertices[v + 1] * scale.y, meshVertices[v + 2] * scale.z);
                    customMeshShape->addPoint(scaledVertex);
                }
            }
        }

        // Set up the dynamics of your tree object
        btTransform treeTransform;
        treeTransform.setIdentity();
        treeTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btScalar treeMass = mass;
        btVector3 treeInertia(0, 0, 0);
        customMeshShape->calculateLocalInertia(treeMass, treeInertia);
        btDefaultMotionState* objectMotionState = new btDefaultMotionState(treeTransform);
        btRigidBody::btRigidBodyConstructionInfo highPolyStaticRigidBodyCI(treeMass, objectMotionState, customMeshShape, treeInertia);
        
        btRigidBody* highPolyStaticRigidBodyPtr = new btRigidBody(highPolyStaticRigidBodyCI);
        highPolyDynamicRigidBody = std::make_shared<btRigidBody*>(highPolyStaticRigidBodyPtr);

        dynamicsWorld->addRigidBody(*highPolyDynamicRigidBody);
    }

    void makePhysicsDynamic(CollisionShapeType shapeType = CollisionShapeType::Box) {
        isDynamic = true;

        if (shapeType == CollisionShapeType::Box)
            createDynamicBox(scale.x, scale.y, scale.z);
        else if (shapeType == CollisionShapeType::HighPolyMesh)
            createDynamicMesh();
    }

    void makePhysicsStatic(CollisionShapeType shapeType = CollisionShapeType::None) {
        isDynamic = false;
 
        if (shapeType == CollisionShapeType::Box)
        {
            createStaticBox(scale.x, scale.y, scale.z);
        }
        else if (shapeType == CollisionShapeType::HighPolyMesh)
            createStaticMesh();

    }

    void reloadRigidBody() {
        if (isDynamic)
            makePhysicsDynamic(*currentCollisionShapeType);
        else
            makePhysicsStatic(*currentCollisionShapeType);
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

            setRot(rotation);
            setScale(scale);

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

            if (hasModel())
            {
                Matrix transformMatrix = MatrixIdentity();
                transformMatrix = MatrixMultiply(transformMatrix, MatrixScale(scale.x, scale.y, scale.z));
                transformMatrix = MatrixMultiply(transformMatrix, MatrixRotateXYZ(rotation));
                transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(position.x, position.y, position.z));

                if (model.meshes != nullptr)
                    bounds = GetMeshBoundingBox(model.meshes[0]);
                
                bounds.min = Vector3Transform(bounds.min, transformMatrix);
                bounds.max = Vector3Transform(bounds.max, transformMatrix);
            }
            
            if (hasModel()) {
                if (!inFrustum()) {
                    return; // Early return if not in the frustum
                }
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
            Color entity_color_raylib = (Color){
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
    // std::lock_guard<std::mutex> lock(script_mutex);

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
                _hitInfo.entity = &entity;
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


    return _hitInfo;
}
