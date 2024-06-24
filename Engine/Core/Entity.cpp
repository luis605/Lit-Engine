py::scoped_interpreter guard{};

py::module entity_module = py::module_::create_extension_module("entity_module", nullptr, new PyModuleDef());

RLFrustum cameraFrustum;
void InitFrustum() {
    cameraFrustum = RLFrustum();
}

void UpdateFrustum() {
    cameraFrustum.Extract();
}

bool PointInFrustum(const Vector3& point) {
    return cameraFrustum.PointIn(point);
}

bool SphereInFrustum(const Vector3& position, float radius) {
    return cameraFrustum.SphereIn(position, radius);
}

bool AABBoxInFrustum(const Vector3& min, const Vector3& max) {
    return cameraFrustum.AABBoxIn(min, max);
}

class __attribute__((visibility("default"))) Entity {
public:
    bool initialized = false;
    std::string name = "Entity";
    float size = 1;
    LitVector3 position = { 0, 0, 0 };
    LitVector3 rotation = { 0, 0, 0 };
    LitVector3 scale = { 1, 1, 1 };

    LitVector3 relativePosition = { 0, 0, 0 };
    LitVector3 relativeRotation = { 0, 0, 0 };
    LitVector3 relativeScale = { 1, 1, 1 };

    LitVector3 inertia = {0, 0, 0};

    std::string script;
    std::string scriptIndex;
    fs::path modelPath;

    Model model;
    Model LodModels[4] = { };

    BoundingBox bounds;
    BoundingBox constBounds;

    fs::path surfaceMaterialPath;
    SurfaceMaterial surfaceMaterial;

    float tiling[2] = { 1.0f, 1.0f };
    float mass = 1;
    float friction = 1;
    float damping = 0;

    bool collider = true;
    bool visible = true;
    bool isChild = false;
    bool isParent = false;
    bool running = false;
    bool running_first_time = false;
    bool calcPhysics = false;
    bool isDynamic = false;
    bool lodEnabled = true;

    enum ObjectTypeEnum {
        ObjectType_None,
        ObjectType_Cube,
        ObjectType_Cone,
        ObjectType_Cylinder,
        ObjectType_Plane,
        ObjectType_Sphere,
        ObjectType_Torus
    };
    ObjectTypeEnum ObjectType;

    enum CollisionShapeType {
        Box           = 0,
        HighPolyMesh  = 1,
        None          = 2
    };
    std::shared_ptr<CollisionShapeType> currentCollisionShapeType = std::make_shared<CollisionShapeType>(None);

    int id = 0;

    Entity* parent = nullptr;

    std::vector<int> lightsChildren;    // Store the indices
    std::vector<int> entitiesChildren;  // Store the indices

    std::vector<Entity*>      instances;

private:
    std::shared_ptr<btCollisionShape> rigidShape;
    std::shared_ptr<btConvexHullShape> customMeshShape;
    std::shared_ptr<btDefaultMotionState> boxMotionState;
    std::shared_ptr<btRigidBody> rigidBody;
    LitVector3 backupPosition                      = position;

    Matrix *transforms                             = nullptr;
    Material matInstances;

    Shader* entityShader = &shader;

    std::string scriptContent;

    #pragma GCC visibility push(default)
    py::object entityObj;
    py::dict locals;
    py::module scriptModule;
    #pragma GCC visibility pop

    bool entityOptimized = false;

public:
    Entity(std::string name = "entity", LitVector3 position = {0, 0, 0}, LitVector3 scale = { 1, 1, 1 },
    LitVector3 rotation = { 0, 0, 0 }) : name(name), position(position), scale(scale), rotation(rotation) {
        initialized = true;
    }

    bool operator==(const Entity& other) const {
        return this->id == other.id;
    }

    void addInstance(Entity* instance) {
        instances.emplace_back(instance);

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

    bool hasInstances() {
        return !instances.empty();
    }

    void calculateInstance(int index) {
        if (index < 0 || index >= instances.size()) return;

        Entity* entity = instances.at(index);

        Matrix translation = MatrixTranslate(entity->position.x, entity->position.y, entity->position.z);
        Matrix rotation = MatrixRotateXYZ(Vector3{ DEG2RAD * entity->rotation.x, DEG2RAD * entity->rotation.y, DEG2RAD * entity->rotation.z });    

        transforms[index] = MatrixMultiply(rotation, translation);

        matInstances = LoadMaterialDefault();
    }
        
    void addEntityChild(int newEntityIndex) {
        Entity& newEntity = entitiesListPregame.at(newEntityIndex);

        if (&newEntity == this) {
            std::cerr << "Error: newEntity is parent" << std::endl;
            return;
        }

        if (newEntity.isChild && newEntity.parent != nullptr) {
            auto it = std::find(newEntity.parent->entitiesChildren.begin(), newEntity.parent->entitiesChildren.end(), newEntityIndex);

            if (it != newEntity.parent->entitiesChildren.end()) {
                newEntity.parent->entitiesChildren.erase(it);
            }
        }

        newEntity.isChild = true;
        newEntity.parent = this;
        newEntity.relativePosition = {
            newEntity.position.x - this->position.x,
            newEntity.position.y - this->position.y,
            newEntity.position.z - this->position.z
        };

       entitiesChildren.emplace_back(newEntityIndex);
    }

    void addLightChild(int newLightIndex) {
        LightStruct& newLight = lights.at(newLightIndex);

        if (newLight.isChild && newLight.parent != nullptr) {
            auto it = std::find(newLight.parent->lightsChildren.begin(), newLight.parent->lightsChildren.end(), newLightIndex);
            
            if (it != newLight.parent->lightsChildren.end()) {
                newLight.parent->lightsChildren.erase(it);
            }
        }

        newLight.isChild = true;
        newLight.parent = this;
        newLight.light.relativePosition = {
            newLight.light.position.x - this->position.x,
            newLight.light.position.y - this->position.y,
            newLight.light.position.z - this->position.z
        };

        lightsChildren.emplace_back(newLightIndex);
    }

    void updateChildren() {
        for (int entityChildIndex : entitiesChildren) {
#ifndef GAME_SHIPPING
            if (!inGamePreview) updateEntityChild(entitiesListPregame.at(entityChildIndex));
            else                updateEntityChild(entitiesList.at(entityChildIndex));
#else
            updateEntityChild(entitiesList.at(entityChildIndex));
#endif
        }

        for (int lightChildIndex : lightsChildren) {
            updateLightChild(lights.at(lightChildIndex));
        }
    }

    void updateEntityChild(Entity& entity) {
        if (!entity.initialized) return;

    #ifndef GAME_SHIPPING
        if (&entity == selectedEntity) {
            entity.render();
            return;
        };
    #endif

        if (entity.parent != this || !entity.parent->initialized) entity.parent = this;

        entity.position = this->position + entity.relativePosition;
        entity.render();
    }

    void updateLightChild(LightStruct& lightStruct) {
    #ifndef GAME_SHIPPING
        if (&lightStruct == selectedLight && selectedGameObjectType == "light" && !inGamePreview) return;
    #endif

        if (lightStruct.parent != this || !lightStruct.parent->initialized) lightStruct.parent = this;

        lightStruct.light.position = glm::vec3(this->position.x, this->position.y, this->position.z) + lightStruct.light.relativePosition;
    }

    void makeChildrenInstances() {
        // for (Entity* entity : entitiesChildren) {
        //     addInstance(entity);
        //     entity->makeChildrenInstances();
        // }
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
        if (entityShader == nullptr)
            model.materials[0].shader = shader;
        else
            model.materials[0].shader = *entityShader;
    }

    void loadModel(const char* filename, const char* textureFilename = NULL) {
        model = LoadModel(filename);
    }

    void ReloadTextures(bool force_reload = false) {
        if (!surfaceMaterial.diffuseTexturePath.empty() || force_reload) {
            if (surfaceMaterial.diffuseTexture.activatedMode == 0) {
                Texture2D& diffuse = surfaceMaterial.diffuseTexture.getTexture2D();
                model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
            } else if (surfaceMaterial.diffuseTexture.activatedMode == 1) {
                VideoPlayer& videoPlayer = surfaceMaterial.diffuseTexture.getVideoPlayer();
                videoPlayer.Update();
                model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = videoPlayer.GetTexture();
            } else model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{ };

            if (lodEnabled) {
                for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                    if (IsModelReady(LodModels[i])) {
                        LodModels[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture;
                    }
                }
            }
        }

        if (!surfaceMaterial.normalTexturePath.empty() || force_reload) {
            if (surfaceMaterial.normalTexture.activatedMode == 0) {
                Texture2D& normal = surfaceMaterial.normalTexture.getTexture2D();
                model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = normal;
            } else if (surfaceMaterial.normalTexture.activatedMode == 1) {
                VideoPlayer& videoPlayer = surfaceMaterial.normalTexture.getVideoPlayer();
                videoPlayer.Update();
                model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = videoPlayer.GetTexture();
            } else model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = Texture2D{ };

            if (lodEnabled) {
                for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                    if (IsModelReady(LodModels[i])) {
                        LodModels[i].materials[0].maps[MATERIAL_MAP_NORMAL].texture = model.materials[0].maps[MATERIAL_MAP_NORMAL].texture;
                    }
                }
            }
        }

        if (!surfaceMaterial.roughnessTexturePath.empty() || !force_reload) {
            if (surfaceMaterial.roughnessTexture.activatedMode == 0) {
                Texture2D& roughness = surfaceMaterial.roughnessTexture.getTexture2D();
                model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = roughness;
            } else if (surfaceMaterial.roughnessTexture.activatedMode == 1) {
                VideoPlayer& videoPlayer = surfaceMaterial.roughnessTexture.getVideoPlayer();
                videoPlayer.Update();
                model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = videoPlayer.GetTexture();
            } else model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = Texture2D{ };

            if (lodEnabled) {
                for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                    if (IsModelReady(LodModels[i])) {
                        LodModels[i].materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture;
                    }
                }
            }
        }

        if (!surfaceMaterial.aoTexturePath.empty() || !force_reload) {
            if (surfaceMaterial.aoTexture.activatedMode == 0) {
                Texture2D& ao = surfaceMaterial.aoTexture.getTexture2D();
                model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = ao;
            } else if (surfaceMaterial.aoTexture.activatedMode == 1) {
                VideoPlayer& videoPlayer = surfaceMaterial.aoTexture.getVideoPlayer();
                videoPlayer.Update();
                model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = videoPlayer.GetTexture();
            } else model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = Texture2D{ };

            if (lodEnabled) {
                for (int i = 0; i < sizeof(LodModels)/sizeof(LodModels[0]); i++) {
                    if (IsModelReady(LodModels[i])) {
                        LodModels[i].materials[0].maps[MATERIAL_MAP_OCCLUSION].texture = model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture;
                    }
                }
            }
        }
    }

    void OptimizeEntityMemory() {
        for (int modelsIndex = 0; modelsIndex < sizeof(LodModels)/sizeof(LodModels[0]); modelsIndex++) {
            if (IsModelReady(LodModels[modelsIndex])) continue;
            for (int index = 0; index < LodModels[modelsIndex].meshCount; index++) {
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
            free(LodModels[modelsIndex].bones);
        }

        entityOptimized = true;
    }

    void setModel(const fs::path& path = "", const Model& entityModel = Model()) {
        modelPath = path;
        model = modelPath.empty() ? entityModel : LoadModel(path.c_str());

        if (!IsModelReady(model)) {
            TraceLog(LOG_WARNING, "Could not set invalid model.");
            return;
        };

        setShader(*entityShader);

        constBounds = GetMeshBoundingBox(model.meshes[0]);

        if (lodEnabled) {
            std::vector<uint32_t> indices;
            std::vector<Vector3> vertices;

            for (size_t i = 0; i < model.meshes[0].vertexCount; ++i) {
                size_t baseIndex = i * 3;
                float x = model.meshes[0].vertices[baseIndex];
                float y = model.meshes[0].vertices[baseIndex + 1];
                float z = model.meshes[0].vertices[baseIndex + 2];

                size_t ix;
                if (model.meshes[0].indices) ix = model.meshes[0].indices[i];
                else                         ix = i;

                vertices.emplace_back(Vector3{x, y, z});
                indices.emplace_back(ix);
            }

            if (vertices.size() > 50) {
                OptimizedMeshData data(indices, vertices);

                this->LodModels[0] = this->model;

                data = OptimizeMesh(indices, vertices, 0.8f);
                this->LodModels[1] = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, model.meshes[0]));

                data = OptimizeMesh(indices, vertices, 0.6f);
                this->LodModels[2] = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, model.meshes[0]));

                data = OptimizeMesh(indices, vertices, 0.3f);
                this->LodModels[3] = LoadModelFromMesh(generateLODMesh(data.Vertices, data.Indices, model.meshes[0]));

                OptimizeEntityMemory();
            }
        }

        isDynamic ? makePhysicsDynamic() : makePhysicsStatic();

        ReloadTextures();
    }

    bool hasModel() {
        return IsModelReady(model);
    }

    void setShader(Shader& newShader) {
        entityShader = &newShader;
        if (IsModelReady(model)) model.materials[0].shader = newShader;

        for (int index = 0; index < 4; index++) {
            if (IsModelReady(LodModels[index])) {
                LodModels[index].materials[0].shader = newShader;
            }
        }
    }

    void setupScript(LitCamera* rendering_camera) {
        if (script.empty() && scriptIndex.empty()) return;
        running = true;

        if (!Entity_already_registered) {
            Entity_already_registered = true;
            py::class_<Entity>(entity_module, "Entity")
                .def(py::init([](py::args args, py::kwargs kwargs) {
                    LitVector3 position{0, 0, 0};
                    std::string modelPath = "";

                    if (args.size() > 0)               position = py::cast<LitVector3>(args[0]);
                    if (kwargs.contains("modelPath"))  modelPath = py::cast<std::string>(kwargs["modelPath"]);

                    Entity entity;
                    entity.setColor(RAYWHITE);
                    entity.setScale(LitVector3{1, 1, 1});
                    entity.setName("New Entity");

                    if (kwargs.contains("collider"))   entity.collider = py::cast<bool>(kwargs["collider"]);

                    if (!modelPath.empty()) entity.setModel(modelPath.c_str());

                    entity.setPos(position);
                    entitiesListPregame.emplace_back(entity);

                    return entitiesListPregame.back();
                }))

                .def_property("name", &Entity::getName, &Entity::setName)
                .def_readwrite("position", &Entity::position)
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
        entityObj = py::cast(this);
    
        locals = py::dict(
            "entity"_a = entityObj,
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
        scriptContent = readFileToString(script);
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
                    scriptContent = element[scriptIndex].get<std::string>();
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
            scriptModule = py::module::import("__main__");

            for (auto item : locals) {
                scriptModule.attr(item.first) = item.second;
            }

            std::string scriptContent_copy = scriptContent;
            py::eval<py::eval_statements>(scriptContent_copy, scriptModule.attr("__dict__"));
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
            isDynamic ? createDynamicBox() : createStaticBox();
        }

        else if (CollisionShapeType::HighPolyMesh == *currentCollisionShapeType && this->running) {
            isDynamic ? createDynamicMesh(false) : createStaticMesh(true);
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

    void createStaticBox() {
        isDynamic = false;

        rigidShape = std::make_shared<btBoxShape>(btVector3(scale.x * scaleFactorRaylibBullet, scale.y * scaleFactorRaylibBullet, scale.z * scaleFactorRaylibBullet));
        if (rigidBody) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

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

        if (rigidBody.get()) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

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

    void createDynamicBox() {
        isDynamic = true;

        if (rigidBody.get()) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

        rigidShape = std::make_shared<btBoxShape>(btVector3(scale.x * scaleFactorRaylibBullet, scale.y * scaleFactorRaylibBullet, scale.z * scaleFactorRaylibBullet));

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

        if (shapeType == CollisionShapeType::Box)                createDynamicBox();
        else if (shapeType == CollisionShapeType::HighPolyMesh)  createDynamicMesh();
    }

    void makePhysicsStatic(CollisionShapeType shapeType = CollisionShapeType::None) {
        isDynamic = false;
 
        if (shapeType == CollisionShapeType::Box)                createStaticBox();
        else if (shapeType == CollisionShapeType::HighPolyMesh)  createStaticMesh();
    }

    void reloadRigidBody() {
        isDynamic ? makePhysicsDynamic(*currentCollisionShapeType) : makePhysicsStatic(*currentCollisionShapeType);
    }

    void resetPhysics() {
        if (rigidBody && rigidBody != nullptr) {
            rigidBody->setLinearVelocity(btVector3(0, 0, 0));
            rigidBody->setAngularVelocity(btVector3(0, 0, 0));

            setPos(backupPosition);
        }
    }

    void printPosition() {
        std::cout << "Position: " << position.x << ", " << position.y << ", " << position.z << "\n";
    }

    bool inFrustum() {
        UpdateFrustum();
        return AABBoxInFrustum(bounds.min, bounds.max);
    }

    void render() {
        if (!this->initialized) return; //delete this;
        if (!hasModel()) initializeDefaultModel();

        updateChildren();

        if (calcPhysics) {
            if (*currentCollisionShapeType != CollisionShapeType::None && isDynamic) {
                setPos(position);
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

        instances.empty() ? renderSingleModel() : renderInstanced();
    }

private:
    void renderInstanced() {
        PassSurfaceMaterials();

        glUseProgram((GLuint)instancingShader.id);

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
    
    void PassSurfaceMaterials() {
        if (surfaceMaterialUBO != 0) {
            glDeleteBuffers(1, &surfaceMaterialUBO);
            surfaceMaterialUBO = 0;
        }

        glGenBuffers(1, &surfaceMaterialUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, surfaceMaterialUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(SurfaceMaterial), &this->surfaceMaterial, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        constexpr GLuint bindingPoint = 0;

        glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, surfaceMaterialUBO);

        glUseProgram(entityShader->id);

        glUniform1i(glGetUniformLocation(entityShader->id, "normalMapInit"), !surfaceMaterial.normalTexturePath.empty());
        glUniform1i(glGetUniformLocation(entityShader->id, "roughnessMapInit"), !surfaceMaterial.roughnessTexturePath.empty());

        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }


};

bool operator==(const Entity& e, const Entity* ptr) {
    return &e == ptr;
}

#ifndef GAME_SHIPPING
    void AddEntity(
        bool createimmediatly = false,
        bool isChild = false,
        const fs::path& modelPath = "",
        const Model& model = LoadModelFromMesh(GenMeshCube(1,1,1)),
        const std::string& name = "Unnamed Entity"
    ) {
        Entity entityCreate;
        entityCreate.setColor(WHITE);
        entityCreate.setScale(Vector3{1, 1, 1});
        entityCreate.setName(name);
        entityCreate.isChild = isChild;
        entityCreate.setModel(modelPath, model);
        entityCreate.setShader(shader);
        entityCreate.id = entitiesListPregame.size() + lights.size() + 1;

        entitiesListPregame.emplace_back(std::move(entityCreate));
        selectedGameObjectType = "entity";
        selectedEntity = &entitiesListPregame.back();
    }
#endif