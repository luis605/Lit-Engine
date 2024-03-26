#include "../../include_all.h"

std::string getFileExtension(const std::string& filePath) {
    std::filesystem::path pathObj(filePath);

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

std::string read_file_to_string(const std::string& filename) {
    std::ifstream file(filename);
    std::stringstream buffer;
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

class Entity {
public:
    bool initialized = false;
    std::string name = "Entity";
    float size = 1;
    LitVector3 position = { 0, 0, 0 };
    LitVector3 rotation = { 0, 0, 0 };
    LitVector3 scale = { 1, 1, 1 };

    LitVector3 relativePosition = { 0, 0, 0 };
    LitVector3 relative_rotation = { 0, 0, 0 };
    LitVector3 relative_scale = { 1, 1, 1 };

    std::string script = "";
    std::string scriptIndex = "";
    std::string model_path = "";
    Model model;

    BoundingBox bounds;
    BoundingBox constBounds;

    fs::path texturePath;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> texture;

    fs::path normalTexturePath;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> normalTexture;

    fs::path roughnessTexturePath;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> roughnessTexture;

    fs::path aoTexturePath;
    std::variant<Texture2D, std::unique_ptr<VideoPlayer>> aoTexture;

    fs::path surfaceMaterial_path;
    SurfaceMaterial surfaceMaterial;

    float tiling[2] = { 1.0f, 1.0f };

    bool collider = true;
    bool visible = true;
    bool isChild = false;
    bool isParent = false;
    bool running = false;
    bool running_first_time = false;
    bool calcPhysics = false;
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
    std::vector<std::variant<Entity*, Light*, Text*, LitButton*>> children;

    enum CollisionShapeType
    {
        Box           = 0,
        HighPolyMesh  = 1,
        None          = 2
    };

    std::shared_ptr<CollisionShapeType> currentCollisionShapeType = std::make_shared<CollisionShapeType>(None);

private:
    std::shared_ptr<btCollisionShape> rigidShape;
    std::shared_ptr<btConvexHullShape> customMeshShape;
    std::shared_ptr<btDefaultMotionState> boxMotionState;
    std::shared_ptr<btRigidBody> rigidBody;
    LitVector3 backupPosition                      = position;

    std::vector<Entity*> instances;
    Matrix *transforms                             = nullptr;
    Material matInstances;

    Shader* entity_shader;

    py::object entity_obj;
    std::string script_content;
    py::dict locals;
    py::module scriptModule;
    bool entityOptimized = false;

public:
    Entity(LitVector3 scale = { 1, 1, 1 }, LitVector3 rotation = { 0, 0, 0 }, std::string name = "entity",
    LitVector3 position = {0, 0, 0}, std::string script = "")
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
        this->relativePosition = other.relativePosition;
        this->relative_rotation = other.relative_rotation;
        this->relative_scale = other.relative_scale;
        this->script = other.script;
        this->scriptIndex = other.scriptIndex;
        this->model_path = other.model_path;
        this->ObjectType = other.ObjectType;
        this->model = other.model;
        this->bounds = other.bounds;
        this->constBounds = other.constBounds;
        this->tiling[0] = other.tiling[0];
        this->tiling[1] = other.tiling[1];

        this->currentCollisionShapeType     = std::make_shared<CollisionShapeType>(*other.currentCollisionShapeType);

        if (other.rigidBody && other.rigidBody != nullptr) {
            this->rigidBody = std::move(other.rigidBody);
        }

        if (other.boxMotionState && other.boxMotionState != nullptr) {
            this->boxMotionState = std::move(other.boxMotionState);
        }

        if (other.rigidShape && other.rigidShape != nullptr) {
            this->rigidShape = std::move(other.rigidShape);
        }

        if (other.customMeshShape && other.customMeshShape != nullptr) {
            this->customMeshShape = std::move(other.customMeshShape);
        }

        this->texturePath                  = other.texturePath;
        this->texture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity texture variant");
        }, other.texture);


        this->normalTexturePath = other.normalTexturePath;
        this->normalTexture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity normal texture variant");
        }, other.normalTexture);


        this->roughnessTexturePath = other.roughnessTexturePath;
        this->roughnessTexture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity roughness texture variant");
        }, other.roughnessTexture);

        this->aoTexturePath = other.aoTexturePath;
        this->aoTexture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<T, Texture>) {
                return value; // Texture remains the same
            } else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>(); // Handle null pointer case
            else TraceLog(LOG_WARNING, "Bad Type - Entity AO texture variant");
        }, other.aoTexture);

        this->surfaceMaterial_path = other.surfaceMaterial_path;
        this->surfaceMaterial = other.surfaceMaterial;
        this->collider = other.collider;
        this->visible = other.visible;
        this->isChild = other.isChild;
        this->isParent = other.isParent;
        this->running = other.running;
        this->running_first_time = other.running_first_time;
        this->calcPhysics = other.calcPhysics;
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
        this->relativePosition = other.relativePosition;
        this->relative_rotation = other.relative_rotation;
        this->relative_scale = other.relative_scale;
        this->script = other.script;
        this->scriptIndex = other.scriptIndex;
        this->model_path = other.model_path;
        this->ObjectType = other.ObjectType;
        this->model = other.model;
        this->bounds = other.bounds;
        this->constBounds = other.constBounds;
        this->texturePath = other.texturePath;
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

        this->normalTexturePath = other.normalTexturePath;
        this->normalTexture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity normal texture variant");
        }, other.normalTexture);

        this->roughnessTexturePath = other.roughnessTexturePath;
        this->roughnessTexture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity roughness texture variant");
        }, other.roughnessTexture);

        this->aoTexturePath = other.aoTexturePath;
        this->aoTexture = std::visit([](const auto& value) -> std::variant<Texture, std::unique_ptr<VideoPlayer, std::default_delete<VideoPlayer>>> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Texture>) return value;
            else if constexpr (std::is_same_v<T, std::unique_ptr<VideoPlayer>>)
                if (value) return std::make_unique<VideoPlayer>(*value);
                else return std::unique_ptr<VideoPlayer>();
            else TraceLog(LOG_WARNING, "Bad Type - Entity ao texture variant");
        }, other.aoTexture);

        this->surfaceMaterial = other.surfaceMaterial;
        this->surfaceMaterial_path = other.surfaceMaterial_path;
        this->collider = other.collider;

        this->currentCollisionShapeType     = std::make_shared<CollisionShapeType>(*other.currentCollisionShapeType);

        if (other.rigidBody && other.rigidBody != nullptr) {
            this->rigidBody = std::move(other.rigidBody);
        }

        if (other.boxMotionState && other.boxMotionState != nullptr) {
            this->boxMotionState = std::move(other.boxMotionState);
        }

        if (other.rigidShape && other.rigidShape != nullptr) {
            this->rigidShape = std::move(other.rigidShape);
        }

        if (other.customMeshShape && other.customMeshShape != nullptr) {
            this->customMeshShape = std::move(other.customMeshShape);
        }

        this->visible = other.visible;
        this->isChild = other.isChild;
        this->isParent = other.isParent;
        this->running = other.running;
        this->running_first_time = other.running_first_time;
        this->calcPhysics = other.calcPhysics;
        this->isDynamic = other.isDynamic;
        this->mass = other.mass;
        this->inertia = other.inertia;
        this->id = other.id;
        this->parent = nullptr;
        this->children = other.children;

        return *this;
    }

    Entity(std::vector<Entity>& entitiesListPregame) {
        entitiesListPregame.push_back(*this);
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

        instancingShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(instancingShader, "mvp");
        instancingShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(instancingShader, "viewPos");
        instancingShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(instancingShader, "instanceTransform");
    }

    bool hasInstances()
    {
        return instances.empty();
    }

    void calculateInstance(int index) {
        if (index < 0 || index >= instances.size()) return;

        Entity* entity = instances.at(index);

        Matrix translation = MatrixTranslate(entity->position.x, entity->position.y, entity->position.z);
        Matrix rotation = MatrixRotateXYZ(Vector3{ DEG2RAD * entity->rotation.x, DEG2RAD * entity->rotation.y, DEG2RAD * entity->rotation.z });    

        transforms[index] = MatrixMultiply(rotation, translation);

        matInstances = LoadMaterialDefault();
    }
        
    void addChild(Entity& entityChild) {
        Entity* newChild = new Entity(entityChild);

        newChild->isChild = true;
        newChild->parent = this;
        newChild->relativePosition = {
            newChild->position.x - this->position.x,
            newChild->position.y - this->position.y,
            newChild->position.z - this->position.z
        };

        children.emplace_back(newChild);
    }


    void addChild(Light& lightChild) {
        Light newChild = Light(lightChild);

        newChild.isChild = true;
        newChild.relativePosition = {
            newChild.position.x - this->position.x,
            newChild.position.y - this->position.y,
            newChild.position.z - this->position.z
        };

        lights.emplace_back(std::move(newChild));
        children.emplace_back(&lights.back());
    }

    void update_children() {
        if (children.empty()) return;
        
        for (auto& childVariant : children) {
            if (auto* entity = std::get_if<Entity*>(&childVariant)) {
                update_entity_child(*entity);
            } else if (Light* lightChild = *std::get_if<Light*>(&childVariant)) {
                auto it = std::find_if(lights.begin(), lights.end(),
                    [&](const Light& light) { return light.id == lightChild->id; });

                int distance = (it != lights.end()) ? std::distance(lights.begin(), it) : -1;

                update_light_child(&lights[distance]);
            }
        }
    }

    void update_entity_child(Entity* entity) {
        entity->render();

    #ifndef GAME_SHIPPING
        if (entity == selectedEntity) return;
    #endif

        entity->position = this->position + entity->relativePosition;
        entity->update_children();
    }

    void update_light_child(Light* light) {
    #ifndef GAME_SHIPPING
        if (light == selectedLight && selectedGameObjectType == "light") return;
    #endif

        if (light) {
            light->position = glm::vec3(this->position.x, this->position.y, this->position.z) + light->relativePosition;
        }
    }

    void makeChildrenInstances() {
        for (const auto& childVariant : children) {
            if (auto childEntity = std::get_if<Entity*>(&childVariant)) {
                addInstance(*childEntity);
                (*childEntity)->makeChildrenInstances();
            }
        }
    }

    void remove() {
        for (auto& childVariant : children) {
            std::visit([](auto& child) { delete child; }, childVariant);
        }

        entitiesListPregame.erase(
            std::remove_if(entitiesListPregame.begin(), entitiesListPregame.end(),
                [this](const Entity& entity) {
                    return entity.id == this->id;
                }),
            entitiesListPregame.end());
    }

    Color getColor() {
        return (Color) {
            static_cast<unsigned char>(surfaceMaterial.color.x * 255),
            static_cast<unsigned char>(surfaceMaterial.color.y * 255),
            static_cast<unsigned char>(surfaceMaterial.color.z * 255),
            static_cast<unsigned char>(surfaceMaterial.color.w * 255)
        };
    }

    void setColor(Color newColor) {
        surfaceMaterial.color = {
            newColor.r / 255,
            newColor.g / 255,
            newColor.b / 255,
            newColor.a / 255
        };
    }

    void setName(const std::string& newName) {
        name = newName;
    }

    std::string getName() const {
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
        if (!texturePath.empty() || !force_reload) {
            if (auto diffuseTexture = std::get_if<Texture2D>(&texture)) {
                model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *diffuseTexture;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *diffuseTexture;
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

        if (!normalTexturePath.empty() || !force_reload) {
            if (auto normal = std::get_if<Texture2D>(&normalTexture)) {
                model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = *normal;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_NORMAL].texture = *normal;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&normalTexture)) {
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

        if (!roughnessTexturePath.empty() || !force_reload) {
            if (auto roughness = std::get_if<Texture2D>(&roughnessTexture)) {
                model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = *roughness;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = *roughness;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&roughnessTexture)) {
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

        if (!aoTexturePath.empty() || !force_reload) {
            if (auto ao = std::get_if<Texture2D>(&aoTexture)) {
                model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = *ao;

                if (lodEnabled)
                {
                    for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                        if (IsModelReady(LodModels[i]))
                            LodModels[i].materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = *ao;
                    }
                }
            } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&aoTexture)) {
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

        for (int modelsIndex = 0; modelsIndex < sizeof(LodModels)/sizeof(LodModels[0]); modelsIndex++)
        {
            if (IsModelReady(LodModels[modelsIndex])) continue;
            for (int index = 0; index < LodModels[modelsIndex].meshCount; index++)
            {
                free(LodModels[modelsIndex].meshes[index].vertices);
                free(LodModels[modelsIndex].meshes[index].indices);
                free(LodModels[modelsIndex].meshes[index].colors);
                free(LodModels[modelsIndex].meshes[index].normals);
                free(LodModels[modelsIndex].meshes[index].tangents);
                free(LodModels[modelsIndex].meshes[index].texcoords);
                free(LodModels[modelsIndex].meshes[index].boneIds);
                free(LodModels[modelsIndex].meshes[index].boneWeights);
                free(LodModels[modelsIndex].meshes[index].animVertices);
                free(LodModels[modelsIndex].meshes[index].animNormals);
                free(LodModels[modelsIndex].meshes[index].texcoords2);
            }

            free(LodModels[modelsIndex].bindPose);
            free(LodModels[modelsIndex].boneCount);
            free(LodModels[modelsIndex].bones);
        }
    }

    void setModel(const char* modelPath = "", Model entity_model = Model(), Shader default_shader = shader)
    {
        model_path = modelPath;
    
        if (modelPath == "")
            model = entity_model;
        else
            model = LoadModel(modelPath);

        constBounds = GetMeshBoundingBox(model.meshes[0]);

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

        if (isDynamic)
            makePhysicsDynamic();
        else
            makePhysicsStatic();

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
        if (script.empty() && scriptIndex.empty()) return;
        running = true;

        if (!Entity_already_registered) {
            Entity_already_registered = true;
            py::class_<Entity>(entity_module, "Entity")
                .def(py::init([](py::args args, py::kwargs kwargs) {
                    LitVector3 position{0, 0, 0};
                    std::string modelPath = "";

                    if (args.size() > 0) {
                        position = py::cast<LitVector3>(args[0]);
                    }

                    if (kwargs.contains("modelPath")) {
                        modelPath = py::cast<std::string>(kwargs["modelPath"]);
                    }

                    Entity entity;
                    entity.setColor(RAYWHITE);
                    entity.setScale(LitVector3{1, 1, 1});
                    entity.setName("New Entity");

                    if (kwargs.contains("collider")) {
                        entity.collider = py::cast<bool>(kwargs["collider"]);
                    }

                    if (!modelPath.empty()) {
                        entity.setModel(modelPath.c_str());
                    } else {
                        entity.initializeDefaultModel();
                    }

                    entity.setPos(position);
                    entitiesListPregame.push_back(entity);

                    return entitiesListPregame.back();
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
                .def("printPosition", &Entity::printPosition)
                .def("applyForce", &Entity::applyForce)
                .def("applyImpulse", &Entity::applyImpulse)
                .def("setFriction", &Entity::setFriction)
                .def("makeStatic", &Entity::makePhysicsStatic)
                .def("makeDynamic", &Entity::makePhysicsDynamic);
        }

        py::module inputModule = py::module::import("inputModule");
        py::module collisionModule = py::module::import("collisionModule");
        py::module cameraModule = py::module::import("cameraModule");
        py::module physicsModule = py::module::import("physicsModule");
        py::module mouseModule = py::module::import("mouseModule");
        py::module timeModule = py::module::import("timeModule");
        py::module colorModule = py::module::import("colorModule");
        py::module mathModule = py::module::import("mathModule");
        py::module_::import("__main__").attr("entitiesList") = py::cast(entitiesList);
        entity_obj = py::cast(this);
    
        locals = py::dict(
            "entity"_a = entity_obj,
            "IsMouseButtonPressed"_a = inputModule.attr("isMouseButtonPressed"),
            "IsKeyDown"_a = inputModule.attr("isKeyDown"),
            "IsKeyPressed"_a = inputModule.attr("isKeyPressed"),
            "IsKeyUp"_a = inputModule.attr("isKeyUp"),
            "GetMouseMovement"_a = inputModule.attr("getMouseMovement"),
            "KeyboardKey"_a = inputModule.attr("KeyboardKey"),
            "MouseButton"_a = inputModule.attr("MouseButton"),
            "Raycast"_a = collisionModule.attr("raycast"),
            "Vector3"_a = mathModule.attr("Vector3"),
            "Vector2"_a = mathModule.attr("Vector2"),
            "Vector3Scale"_a = mathModule.attr("vector3Scale"),
            "Vector3Distance"_a = mathModule.attr("vector3Distance"),
            "Color"_a = colorModule.attr("Color"),
            "LockMouse"_a = mouseModule.attr("LockMouse"),
            "UnlockMouse"_a = mouseModule.attr("UnlockMouse"),
            "time"_a = py::cast(&time_instance),
            "physics"_a = py::cast(&physics),
            "Lerp"_a = mathModule.attr("lerp"),
            "entitiesList"_a = entitiesList,
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
        json_data = json::parse(decryptedScripts);
    } catch (const json::parse_error& e) {
        return;
    }

    infile.close();

    if (json_data.is_array() && !json_data.empty()) {
        for (const auto& element : json_data) {

            if (element.is_object()) {
                if (element.contains(scriptIndex)) {
                    script_content = element[scriptIndex].get<std::string>();
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
            scriptModule = py::module("__main__");

            for (auto item : locals) {
                scriptModule.attr(item.first) = item.second;
            }

            std::string script_content_copy = script_content;
            py::eval<py::eval_statements>(script_content_copy, scriptModule.attr("__dict__"));
        } catch (const py::error_already_set& e) {
            py::print(e.what());
        }
    }

    void runScript(LitCamera* rendering_camera) {
        if (script.empty() && scriptIndex.empty()) return;

        try {
            if (py::hasattr(scriptModule, "update")) {
                py::object update_func = scriptModule.attr("update");
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
            } else if (rigidBody) {
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
            if (isDynamic) createDynamicBox(scale.x, scale.y, scale.z);
            else createStaticBox(scale.x, scale.y, scale.z);
        }

        else if (CollisionShapeType::HighPolyMesh == *currentCollisionShapeType && this->running) {
            if (isDynamic) createDynamicMesh(false);
            else createStaticMesh(true);
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

        if (rigidBody.get()) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

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
        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::Box);
    }

    void createDynamicMesh(bool generateShape = true) {
        isDynamic = true;

        currentCollisionShapeType = std::make_shared<CollisionShapeType>(CollisionShapeType::HighPolyMesh);

        if (rigidBody.get()) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

        if (generateShape || !customMeshShape.get()) {
            customMeshShape = std::make_shared<btConvexHullShape>();

            for (int m = 0; m < model.meshCount; m++) {
                Mesh mesh = model.meshes[m];
                float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

                for (int v = 0; v < mesh.vertexCount; v += 3) {
                    btVector3 scaledVertex(meshVertices[v] * scale.x, meshVertices[v + 1] * scale.y, meshVertices[v + 2] * scale.z);
                    customMeshShape.get()->addPoint(scaledVertex);
                }
            }
        }

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
            createStaticBox(scale.x, scale.y, scale.z);
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

    void printPosition()
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

        if (calcPhysics) {
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

        glUseProgram((GLuint)instancingShader.id);

        bool normalMapInit = !normalTexturePath.empty();
        glUniform1i(glGetUniformLocation((GLuint)instancingShader.id, "normalMapInit"), normalMapInit);

        bool roughnessMapInit = !roughnessTexturePath.empty();
        glUniform1i(glGetUniformLocation((GLuint)instancingShader.id, "roughnessMapInit"), roughnessMapInit);

        matInstances = LoadMaterialDefault();

        instancingShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(instancingShader, "mvp");
        instancingShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(instancingShader, "viewPos");
        instancingShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(instancingShader, "instanceTransform");

        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = {
            static_cast<unsigned char>(surfaceMaterial.color.x * 255),
            static_cast<unsigned char>(surfaceMaterial.color.y * 255),
            static_cast<unsigned char>(surfaceMaterial.color.z * 255),
            static_cast<unsigned char>(surfaceMaterial.color.w * 255)
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
            bounds.min = Vector3Transform(constBounds.min, transformMatrix);
            bounds.max = Vector3Transform(constBounds.max, transformMatrix);
        }

        if (!inFrustum()) return;

        PassSurfaceMaterials();
        ReloadTextures();
        glUseProgram((GLuint)shader.id);

        glUniform1i(glGetUniformLocation((GLuint)shader.id, "normalMapInit"), !normalTexturePath.empty());
        glUniform1i(glGetUniformLocation((GLuint)shader.id, "roughnessMapInit"), !roughnessTexturePath.empty());

        float distance;

    #ifndef GAME_SHIPPING
        distance = inGamePreview ? Vector3Distance(this->position, camera.position)
                                : Vector3Distance(this->position, sceneCamera.position);
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
                    static_cast<unsigned char>(surfaceMaterial.color.x * 255),
                    static_cast<unsigned char>(surfaceMaterial.color.y * 255),
                    static_cast<unsigned char>(surfaceMaterial.color.z * 255),
                    static_cast<unsigned char>(surfaceMaterial.color.w * 255)});
    }
    
    void PassSurfaceMaterials()
    {
        if (surfaveMaterialUBO != 0) {
            glDeleteBuffers(1, &surfaveMaterialUBO);
            surfaveMaterialUBO = 0;
        }

        glGenBuffers(1, &surfaveMaterialUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, surfaveMaterialUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(SurfaceMaterial), &this->surfaceMaterial, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        GLuint bindingPoint = 0;
        glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, surfaveMaterialUBO);

        glBindBuffer(GL_UNIFORM_BUFFER, surfaveMaterialUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SurfaceMaterial), &this->surfaceMaterial);
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

        while (std::find_if(entitiesList.begin(), entitiesList.end(),
            [newID](const Entity& entity) { return entity.id == newID; }) != entitiesList.end())
        {
            newID++;
        }

        return newID;
    }

    void AddEntity(
        bool createimmediatly = false,
        bool isChild = false,
        const char* model_path = "",
        Model model = LoadModelFromMesh(GenMeshCube(1,1,1)),
        std::string name = "Unnamed Entity"
    )
    {

        Color entity_color_raylib = WHITE;

        Entity entityCreate;
        entityCreate.setColor(entity_color_raylib);
        entityCreate.setScale(Vector3{1, 1, 1});
        entityCreate.setName(name);
        entityCreate.isChild = isChild;
        entityCreate.setModel(model_path, model);
        entityCreate.setShader(shader);

        if (!entitiesListPregame.empty())
        {
            entityCreate.id = GenerateUniqueID(entitiesListPregame);
        }
        else
            entityCreate.id = 0;

        entitiesListPregame.push_back(entityCreate);
        selectedEntity = &entityCreate;

        int last_entityIndex = entitiesListPregame.size() - 1;
        listViewExActive = last_entityIndex;

        create = false;
        canAddEntity = false;
    }
#endif


HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore)
{
    HitInfo hitInfo;
    hitInfo.hit = false;

    Ray ray{origin, direction};

    if (debug)
        DrawRay(ray, RED);

    if (entitiesList.empty())
        return hitInfo;

    float minDistance = 100000000000000000000000000000.0f;

    for (const Entity& entity : entitiesList)
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
                    static_cast<unsigned char>(entity.surfaceMaterial.color.x * 255),
                    static_cast<unsigned char>(entity.surfaceMaterial.color.w * 255),
                    static_cast<unsigned char>(entity.surfaceMaterial.color.y * 255),
                    static_cast<unsigned char>(entity.surfaceMaterial.color.z * 255)
                };
            }
        }
    }

    return hitInfo;
}