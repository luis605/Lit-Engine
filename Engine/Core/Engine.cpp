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
    BoundingBox const_bounds;


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
    bool lodEnabled = true;

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
    float friction = 1;
    float damping = 0;

    Vector3 inertia = {0, 0, 0};

    Model LodModels[4] = { };

    int id = 0;

    Entity* parent = nullptr;
    vector<variant<Entity*, Light*, Text*, LitButton*>> children;

    enum CollisionShapeType
    {
        Box           = 0,
        HighPolyMesh  = 1,
        None          = 2
    };

    std::shared_ptr<CollisionShapeType> currentCollisionShapeType = make_shared<CollisionShapeType>(None);

private:
    std::shared_ptr<btCollisionShape> rigidShape;
    std::shared_ptr<btConvexHullShape> customMeshShape;
    std::shared_ptr<btDefaultMotionState> boxMotionState;
    std::shared_ptr<btRigidBody> rigidBody;
    LitVector3 backupPosition                      = position;
    vector<Entity*> instances;
    Matrix *transforms                             = nullptr;
    Material matInstances;
    int lastIndexCalculated                        = -1;
    Shader* entity_shader;

    py::object entity_obj;
    string script_content;
    py::dict locals;
    py::module script_module;
    bool entityOptimized = false;

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
        this->const_bounds = other.const_bounds;
        this->tiling[0] = other.tiling[0];
        this->tiling[1] = other.tiling[1];

        this->currentCollisionShapeType     = make_shared<CollisionShapeType>(*other.currentCollisionShapeType);

        if (other.rigidBody && other.rigidBody != nullptr) {
            this->rigidBody = std::move(other.rigidBody);
        }

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
        this->const_bounds = other.const_bounds;
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

        if (other.rigidBody && other.rigidBody != nullptr) {
            this->rigidBody = std::move(other.rigidBody);
        }

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

    void OptimizeEntityMemory()
    {
        entityOptimized = true;

        for (int models_index = 0; models_index < sizeof(LodModels)/sizeof(LodModels[0]); models_index++)
        {
            if (IsModelReady(LodModels[models_index])) continue;
            for (int index = 0; index < LodModels[models_index].meshCount; index++)
            {
                free(LodModels[models_index].meshes[index].vertices);
                free(LodModels[models_index].meshes[index].indices);
                free(LodModels[models_index].meshes[index].colors);
                free(LodModels[models_index].meshes[index].normals);
                free(LodModels[models_index].meshes[index].tangents);
                free(LodModels[models_index].meshes[index].texcoords);
                free(LodModels[models_index].meshes[index].boneIds);
                free(LodModels[models_index].meshes[index].boneWeights);
                free(LodModels[models_index].meshes[index].animVertices);
                free(LodModels[models_index].meshes[index].animNormals);
                free(LodModels[models_index].meshes[index].texcoords2);
            }

            free(LodModels[models_index].bindPose);
            free(LodModels[models_index].boneCount);
            free(LodModels[models_index].bones);
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

        const_bounds = GetMeshBoundingBox(model.meshes[0]);

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

        if (vertices.size() > 150 && lodEnabled)
        {
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

        OptimizeEntityMemory();
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
                .def(py::init([](py::args args, py::kwargs kwargs) {
                    LitVector3 position{0, 0, 0};
                    std::string modelPath = "";

                    if (args.size() > 0) {
                        // Position argument is provided
                        position = py::cast<LitVector3>(args[0]);
                    }

                    if (kwargs.contains("modelPath")) {
                        modelPath = py::cast<std::string>(kwargs["modelPath"]);
                    }

                    Entity* entity = new Entity();
                    entity->setColor(RAYWHITE);
                    entity->setScale(LitVector3{1, 1, 1});
                    entity->setName("New Entity");

                    if (kwargs.contains("collider")) {
                        entity->collider = py::cast<bool>(kwargs["collider"]);
                    }

                    if (!modelPath.empty()) {
                        entity->setModel(modelPath.c_str());
                    } else {
                        entity->initializeDefaultModel();
                    }

                    entity->setPos(position);
                    entities_list_pregame.push_back(*entity);

                    return entities_list_pregame.back();
                }))




                .def_property("name", &Entity::getName, &Entity::setName)
                .def_property("position",
                    [](const Entity& entity) { return entity.position; },
                    [](Entity& entity, LitVector3& position) { entity.setPos(position); }
                )
                .def_readwrite("scale", &Entity::scale)
                .def_property("rotation",
                    [](const Entity& entity) { return entity.rotation; },
                    [](Entity& entity, LitVector3& rotation) { entity.setRot(rotation); }
                )
                .def_property("color", &Entity::getColor, &Entity::setColor)
                .def_readwrite("visible", &Entity::visible)
                .def_readwrite("id", &Entity::id)
                .def_readwrite("collider", &Entity::collider)
                .def("print_position", &Entity::print_position)
                .def("applyForce", &Entity::applyForce)
                .def("applyImpulse", &Entity::applyImpulse)
                .def("setFriction", &Entity::setFriction)
                .def("makeStatic", &Entity::makePhysicsStatic)
                .def("makeDynamic", &Entity::makePhysicsDynamic);
        }


        py::module input_module = py::module::import("input_module");
        py::module collisions_module = py::module::import("collisions_module");
        py::module camera_module = py::module::import("camera_module");
        py::module mouse_module = py::module::import("mouse_module");
        py::module time_module = py::module::import("time_module");
        py::module color_module = py::module::import("color_module");
        py::module math_module = py::module::import("math_module");
        py::module_::import("__main__").attr("entities_list") = py::cast(entities_list);

        entity_obj = py::cast(this);
    
        locals = py::dict(
            "entity"_a = entity_obj,
            "IsMouseButtonPressed"_a = input_module.attr("isMouseButtonPressed"),
            "IsKeyDown"_a = input_module.attr("isKeyDown"),
            "IsKeyPressed"_a = input_module.attr("isKeyPressed"),
            "IsKeyUp"_a = input_module.attr("isKeyUp"),
            "GetMouseMovement"_a = input_module.attr("getMouseMovement"),
            "KeyboardKey"_a = input_module.attr("KeyboardKey"),
            "MouseButton"_a = input_module.attr("MouseButton"),
            "Raycast"_a = collisions_module.attr("raycast"),
            "Vector3"_a = math_module.attr("Vector3"),
            "Vector2"_a = math_module.attr("Vector2"),
            "Vector3Scale"_a = math_module.attr("vector3Scale"),
            "Vector3Distance"_a = math_module.attr("vector3Distance"),
            "Color"_a = color_module.attr("Color"),
            "LockMouse"_a = mouse_module.attr("LockMouse"),
            "UnlockMouse"_a = mouse_module.attr("UnlockMouse"),
            "time"_a = py::cast(&time_instance),
            "Lerp"_a = math_module.attr("lerp"),
            "entitiesList"_a = entities_list,
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
            if (rigidBody && rigidBody->getMotionState()) {
                rigidBody->getMotionState()->getWorldTransform(trans);
                btVector3 rigidBodyPosition = trans.getOrigin();
                position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
            }
        } else if (*currentCollisionShapeType == CollisionShapeType::HighPolyMesh) {
            btTransform trans;
            if (rigidBody && rigidBody->getMotionState()) {
                rigidBody->getMotionState()->getWorldTransform(trans);
                btVector3 rigidBodyPosition = trans.getOrigin();
                position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
            }
        }
    }





    
    void calcPhysicsRotation() {
        if (!isDynamic) return;

        if (CollisionShapeType::Box == *currentCollisionShapeType) {
            if (rigidBody) {
                btTransform trans;
                if (rigidBody->getMotionState()) {
                    rigidBody->getMotionState()->getWorldTransform(trans);
                    btQuaternion objectRotation = trans.getRotation();
                    btScalar Roll, Yaw, Pitch;
                    objectRotation.getEulerZYX(Roll, Yaw, Pitch);

                    rotation = (Vector3){ Pitch * RAD2DEG, Yaw * RAD2DEG, Roll * RAD2DEG };
                }
            }
            else if (rigidBody) {
                btTransform trans;
                if (rigidBody->getMotionState()) {
                    rigidBody->getMotionState()->getWorldTransform(trans);
                    btQuaternion objectRotation = trans.getRotation();
                    btScalar Roll, Yaw, Pitch;
                    objectRotation.getEulerZYX(Roll, Yaw, Pitch);

                    rotation = (Vector3){ Pitch * RAD2DEG, Yaw * RAD2DEG, Roll * RAD2DEG };
                }
            }
        }
    }


    void setPos(LitVector3 newPos) {
        position = newPos;

        if (rigidBody) {
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(newPos.x, newPos.y, newPos.z));

            rigidBody->setWorldTransform(transform);
        }
    }



    void setRot(LitVector3 newRot) {
        rotation = newRot;

        if (CollisionShapeType::Box == *currentCollisionShapeType) {
            if (rigidBody) {
                btTransform currentTransform = rigidBody->getWorldTransform();

                btQuaternion newRotation;
                newRotation.setEulerZYX(newRot.z * DEG2RAD, newRot.y * DEG2RAD, newRot.x * DEG2RAD);
                currentTransform.setRotation(newRotation);
                rigidBody->setWorldTransform(currentTransform);
            }
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
                createStaticMesh(true);
        }

    }

    void applyForce(const LitVector3& force) {
        if (rigidBody && isDynamic) {
            rigidBody->setActivationState(ACTIVE_TAG);
            btVector3 btForce(force.x, force.y, force.z);
            rigidBody->applyCentralForce(btForce);
        }
    }

    void applyImpulse(const LitVector3& impulse) {
        if (rigidBody && isDynamic) {
            rigidBody->setActivationState(ACTIVE_TAG);
            btVector3 btImpulse(impulse.x, impulse.y, impulse.z);
            rigidBody->applyCentralImpulse(btImpulse);
        }
    }


    void setFriction(const float& friction) {
        if (rigidBody && isDynamic) {
            rigidBody->setFriction(friction);
        }
    }

    void applyDamping(const float& damping) {
        if (rigidBody && isDynamic) {
            rigidBody->setDamping(damping, damping);
        }
    }

    void updateMass() {
        if (!isDynamic || rigidShape == nullptr) return;

        btScalar btMass = mass;
        btVector3 boxInertia(inertia.x, inertia.y, inertia.z);
        rigidShape->calculateLocalInertia(btMass, boxInertia);
        if (*currentCollisionShapeType == CollisionShapeType::Box && rigidBody && rigidBody != nullptr)
            rigidBody->setMassProps(btMass, boxInertia);
        else if (*currentCollisionShapeType == CollisionShapeType::HighPolyMesh && rigidBody && rigidBody != nullptr)
            rigidBody->setMassProps(btMass, boxInertia);

    }

    void createStaticBox(float x, float y, float z) {
        isDynamic = false;

        rigidShape = std::make_shared<btBoxShape>(btVector3(x * scaleFactorRaylibBullet, y * scaleFactorRaylibBullet, z * scaleFactorRaylibBullet));

        if (rigidBody) {
            physics.dynamicsWorld->removeRigidBody(rigidBody.get());
        }

        btTransform rigidTransform;
        rigidTransform.setIdentity();

        float rollRad = glm::radians(rotation.x);
        float pitchRad = glm::radians(rotation.y);
        float yawRad = glm::radians(rotation.z);

        btQuaternion quaternion;
        quaternion.setEulerZYX(yawRad, pitchRad, rollRad);

        rigidTransform.setRotation(quaternion);
        rigidTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btDefaultMotionState boxMotionState(rigidTransform);
        btRigidBody::btRigidBodyConstructionInfo highPolyStaticRigidBodyCI(0, &boxMotionState, rigidShape.get(), btVector3(0, 0, 0));

        // Use std::make_unique to create the std::unique_ptr
        rigidBody = std::make_unique<btRigidBody>(highPolyStaticRigidBodyCI);
        physics.dynamicsWorld->addRigidBody(rigidBody.get());

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::Box);
    }


    void createStaticMesh(bool generateShape = true) {
        isDynamic = false;

        if (rigidBody.get()) {
            physics.dynamicsWorld->removeRigidBody(rigidBody.get());
        }

        if (generateShape || !customMeshShape.get()) {
            customMeshShape = std::make_shared<btConvexHullShape>();

            for (int m = 0; m < model.meshCount; m++) {
                Mesh mesh = model.meshes[m];
                float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

                for (int v = 0; v < mesh.vertexCount; v += 3) {
                    btVector3 scaledVertex(
                        meshVertices[v]     * scale.x, 
                        meshVertices[v + 1] * scale.y,
                        meshVertices[v + 2] * scale.z
                    );
                    customMeshShape.get()->addPoint(scaledVertex);
                }
            }
        }

        // Set up the dynamics of your rigid object
        btTransform rigidTransform;
        rigidTransform.setIdentity();
        rigidTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btScalar rigidMass = 0.0f;
        btVector3 rigidInertia(0, 0, 0);
        customMeshShape.get()->calculateLocalInertia(rigidMass, rigidInertia);
        boxMotionState = std::make_shared<btDefaultMotionState>(rigidTransform);
        btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(rigidMass, boxMotionState.get(), customMeshShape.get(), rigidInertia);
        rigidBody = std::make_shared<btRigidBody>(rigidBodyCI);
        
        physics.dynamicsWorld->addRigidBody(rigidBody.get());
        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);
    }


    void createDynamicBox(float x, float y, float z) {
        isDynamic = true;

        if (rigidBody.get()) {
            physics.dynamicsWorld->removeRigidBody(rigidBody.get());
        }

        rigidShape = std::make_shared<btBoxShape>(btVector3(x * scaleFactorRaylibBullet, y * scaleFactorRaylibBullet, z * scaleFactorRaylibBullet));

        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btScalar btMass = mass;

        btVector3 localInertia(inertia.x, inertia.y, inertia.z);

        rigidShape->calculateLocalInertia(btMass, localInertia);

        boxMotionState = std::make_shared<btDefaultMotionState>(startTransform);
        btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(btMass, boxMotionState.get(), rigidShape.get(), localInertia);
        rigidBody = std::make_unique<btRigidBody>(rigidBodyCI);
        
        physics.dynamicsWorld->addRigidBody(rigidBody.get());

        currentCollisionShapeType = make_shared<CollisionShapeType>(CollisionShapeType::Box);
    }

    void createDynamicMesh(bool generateShape = true) {
        isDynamic = true;

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);

        if (rigidBody.get()) {
            physics.dynamicsWorld->removeRigidBody(rigidBody.get());
        }

        if (generateShape || !customMeshShape.get()) {
            customMeshShape = std::make_shared<btConvexHullShape>();

            for (int m = 0; m < model.meshCount; m++) {
                Mesh mesh = model.meshes[m];
                float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

                for (int v = 0; v < mesh.vertexCount; v += 3) {
                    // Apply scaling to the vertex coordinates
                    btVector3 scaledVertex(meshVertices[v] * scale.x, meshVertices[v + 1] * scale.y, meshVertices[v + 2] * scale.z);
                    customMeshShape.get()->addPoint(scaledVertex);
                }
            }
        }

        // Set up the dynamics of your rigid object
        btTransform rigidTransform;
        rigidTransform.setIdentity();
        rigidTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btScalar rigidMass = mass;
        btVector3 rigidInertia(0, 0, 0);
        customMeshShape.get()->calculateLocalInertia(rigidMass, rigidInertia);
        btDefaultMotionState* boxMotionState = new btDefaultMotionState(rigidTransform);
        btRigidBody::btRigidBodyConstructionInfo highPolyStaticRigidBodyCI(rigidMass, boxMotionState, customMeshShape.get(), rigidInertia);
        
        rigidBody = std::make_shared<btRigidBody>(highPolyStaticRigidBodyCI);
        physics.dynamicsWorld->addRigidBody(rigidBody.get());
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
        if (rigidBody && rigidBody != nullptr) {
            rigidBody->setLinearVelocity(btVector3(0, 0, 0));
            rigidBody->setAngularVelocity(btVector3(0, 0, 0));

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
        if (!hasModel()) initializeDefaultModel();

        update_children();

        if (calc_physics) {
            if (*currentCollisionShapeType != CollisionShapeType::None && isDynamic) {
                calcPhysicsRotation();
                calcPhysicsPosition();
            }
        } else {
            setPos(position);
            updateMass();
            backupPosition = position;

            setRot(rotation);
            setScale(scale);
        }

        if (!visible) return;

        int tilingLocation = GetShaderLocation(shader, "tiling");
        SetShaderValue(shader, tilingLocation, tiling, SHADER_UNIFORM_VEC2);

        if (!instances.empty()) renderInstanced();
        else renderSingleModel();
    }

private:
    void renderInstanced() {
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

    void renderSingleModel() {
        if (!hasModel()) {
            return;
        }

        Matrix transformMatrix = MatrixMultiply(MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z),
                                                            MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD))),
                                                MatrixTranslate(position.x, position.y, position.z));

        if (model.meshes != nullptr) {
            bounds.min = Vector3Transform(const_bounds.min, transformMatrix);
            bounds.max = Vector3Transform(const_bounds.max, transformMatrix);
        }

        if (!inFrustum()) {
            return;
        }

        PassSurfaceMaterials();
        ReloadTextures();
        glUseProgram((GLuint)shader.id);

        glUniform1i(glGetUniformLocation((GLuint)shader.id, "normalMapInit"), !normal_texture_path.empty());
        glUniform1i(glGetUniformLocation((GLuint)shader.id, "roughnessMapInit"), !roughness_texture_path.empty());

        float distance;

    #ifndef GAME_SHIPPING
        distance = in_game_preview ? Vector3Distance(this->position, camera.position)
                                : Vector3Distance(this->position, scene_camera.position);
    #else
        distance = Vector3Distance(this->position, camera.position);
    #endif

        model.transform = transformMatrix;

        for (Model& lodModel : LodModels) {
            lodModel.transform = transformMatrix;
        }

        int lodLevel = (distance < LOD_DISTANCE_HIGH) ? 0
                    : (distance < LOD_DISTANCE_MEDIUM) ? 1
                    : (distance < LOD_DISTANCE_LOW) ? 2
                    : 3;

        DrawModel(lodEnabled && IsModelReady(LodModels[lodLevel]) ? LodModels[lodLevel] : model,
                Vector3Zero(),
                1,
                (Color){
                    static_cast<unsigned char>(surface_material.color.x * 255),
                    static_cast<unsigned char>(surface_material.color.y * 255),
                    static_cast<unsigned char>(surface_material.color.z * 255),
                    static_cast<unsigned char>(surface_material.color.w * 255)});
    }
    
    void PassSurfaceMaterials()
    {
        if (surface_material_ubo != 0) {
            glDeleteBuffers(1, &surface_material_ubo);
            surface_material_ubo = 0;
        }

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

    int GenerateUniqueID(const std::vector<Entity>& entitiesList)
    {
        int newID = 0;

        // Loop until a unique ID is found
        while (std::find_if(entitiesList.begin(), entitiesList.end(),
            [newID](const Entity& entity) { return entity.id == newID; }) != entitiesList.end())
        {
            newID++;
        }

        return newID;
    }

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
                entity_create.id = GenerateUniqueID(entities_list_pregame);
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


HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore)
{
    HitInfo hitInfo;
    hitInfo.hit = false;

    Ray ray{origin, direction};

    if (debug)
        DrawRay(ray, RED);

    if (entities_list.empty())
        return hitInfo;

    float minDistance = 100000000000000000000000000000.0f;

    for (const Entity& entity : entities_list)
    {
        if (std::find(ignore.begin(), ignore.end(), entity) != ignore.end())
            continue;
        
        if (!entity.collider)
            continue;

        RayCollision entityBounds = GetRayCollisionBox(ray, entity.bounds);

        for (int mesh_i = 0; mesh_i < entity.model.meshCount && entityBounds.hit; mesh_i++)
        {
            RayCollision meshHitInfo = GetRayCollisionMesh(ray, entity.model.meshes[mesh_i], entity.model.transform);

            if (meshHitInfo.hit && meshHitInfo.distance < minDistance)
            {
                minDistance = meshHitInfo.distance;
                
                hitInfo.hit = true;
                hitInfo.distance = minDistance;
                hitInfo.entity = &entity;
                hitInfo.worldPoint = meshHitInfo.point;
                hitInfo.worldNormal = meshHitInfo.normal;
                hitInfo.hitColor = {
                    static_cast<unsigned char>(entity.surface_material.color.x * 255),
                    static_cast<unsigned char>(entity.surface_material.color.w * 255),
                    static_cast<unsigned char>(entity.surface_material.color.y * 255),
                    static_cast<unsigned char>(entity.surface_material.color.z * 255)
                };
            }
        }
    }

    return hitInfo;
}
