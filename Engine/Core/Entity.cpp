/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Entity.hpp"
#include <rlFrustum.h>
#include <Engine/Core/LoD.hpp>
#include <Engine/Core/Events.hpp>
#include <Engine/Lighting/lights.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Core/Textures.hpp>

#ifndef GAME_SHIPPING
    #include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#endif // GAME_SHIPPING

py::module createEntityModule() {
    return py::module_::create_extension_module("entityModule", nullptr, new PyModuleDef());
}

py::module entityModule;
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

bool SphereInFrustum(const Vector3& position, const float& radius) {
    return cameraFrustum.SphereIn(position, radius);
}

bool AABBoxInFrustum(const Vector3& min, const Vector3& max) {
    return cameraFrustum.AABBoxIn(min, max);
}

void Entity::addInstance(Entity* instance) {
    instances.emplace_back(instance);
    transforms.reserve(1);

    calculateInstance();

    (*shaderManager.m_instancingShader).locs[SHADER_LOC_MATRIX_MVP] = shaderManager.GetUniformLocation((*shaderManager.m_instancingShader).id, "mvp");
    (*shaderManager.m_instancingShader).locs[SHADER_LOC_VECTOR_VIEW] = shaderManager.GetUniformLocation((*shaderManager.m_instancingShader).id, "viewPos");
    (*shaderManager.m_instancingShader).locs[SHADER_LOC_MATRIX_MODEL] = shaderManager.GetAttribLocation((*shaderManager.m_instancingShader).id, "instanceTransform");
}

bool Entity::hasInstances() const {
    return !instances.empty();
}

void Entity::calculateInstance() {
    Entity* entity = instances.back();

    Matrix translation = MatrixTranslate(entity->position.x, entity->position.y, entity->position.z);
    Matrix rotation = MatrixRotateXYZ(Vector3{ DEG2RAD * entity->rotation.x, DEG2RAD * entity->rotation.y, DEG2RAD * entity->rotation.z });

    transforms.back() = MatrixMultiply(rotation, translation);

    matInstances = LoadMaterialDefault();
}

void Entity::addEntityChild(const int& newEntityIndex) {
    Entity* newEntity = getEntityById(newEntityIndex);

    if (!newEntity) {
        TraceLog(LOG_WARNING, "Cannot add child, since child was not found.");
        return;
    }

    if (newEntity == this) {
        TraceLog(LOG_WARNING, "Entity = parent");
        return;
    }

    if (newEntity->getFlag(Entity::Flag::IS_CHILD) && newEntity->parent != nullptr) {
        auto it = std::find(newEntity->parent->entitiesChildren.begin(), newEntity->parent->entitiesChildren.end(), newEntityIndex);

        if (it != newEntity->parent->entitiesChildren.end()) {
            newEntity->parent->entitiesChildren.erase(it);
        }
    }

    newEntity->setFlag(Entity::Flag::IS_CHILD, true);
    newEntity->parent = this;
    newEntity->relativePosition = {
        newEntity->position.x - this->position.x,
        newEntity->position.y - this->position.y,
        newEntity->position.z - this->position.z
    };

    entitiesChildren.emplace_back(newEntityIndex);
}

void Entity::addLightChild(const int& newLightIndex) {
    LightStruct* newLight = getLightById(newLightIndex);

    if (!newLight) {
        TraceLog(LOG_WARNING, "Cannot add child, since child was not found.");
        return;
    }

    if (newLight->isChild && newLight->parent != nullptr) {
        auto it = std::find(newLight->parent->lightsChildren.begin(), newLight->parent->lightsChildren.end(), newLightIndex);

        if (it != newLight->parent->lightsChildren.end()) {
            newLight->parent->lightsChildren.erase(it);
        }
    }

    newLight->isChild = true;
    newLight->parent = this;
    newLight->lightInfo.relativePosition = {
        newLight->light.position.x - this->position.x,
        newLight->light.position.y - this->position.y,
        newLight->light.position.z - this->position.z
    };

    lightsChildren.emplace_back(newLightIndex);
}

void Entity::updateChildren() {
    // std::cout << entitiesChildren.size() << std::endl;
    for (int entityChildIndex : entitiesChildren) {
#ifndef GAME_SHIPPING
        if (!inGamePreview) updateEntityChild(getEntityById(entityChildIndex), entityChildIndex);
        else                updateEntityChild(getEntityById(entityChildIndex), entityChildIndex);
#else
        updateEntityChild(getEntityById(entityChildIndex), entityChildIndex);
#endif
    }

    for (int lightChildIndex : lightsChildren) {
        updateLightChild(getLightById(lightChildIndex), lightChildIndex);
    }
}

void Entity::updateEntityChild(Entity* entity, const int& entityIndex) {
    if (!entity) {
        TraceLog(LOG_WARNING, "Cannot update child, since child is not found.");

        auto it = entitiesChildren.erase(std::find(entitiesChildren.begin(), entitiesChildren.end(),
                            entityIndex));

        return;
    }

    if (!entity->getFlag(Entity::Flag::INITIALIZED)) {
        TraceLog(LOG_WARNING, "Cannot update child, since child is not initialized.");
        return;
    }

#ifndef GAME_SHIPPING
    if (entity == selectedEntity) {
        entity->render();
        return;
    };
#endif

    if (entity->parent != this || !entity->parent->getFlag(Entity::Flag::INITIALIZED)) entity->parent = this;

    entity->position = this->position + entity->relativePosition;
    entity->render();
}

void Entity::updateLightChild(LightStruct* lightStruct, const int& lightIndex) {
    if (!lightStruct) {
        TraceLog(LOG_WARNING, "Cannot update child, since child is not found.");

        auto it = lightsChildren.erase(std::find(lightsChildren.begin(), lightsChildren.end(),
                            lightIndex));

        return;
    }

#ifndef GAME_SHIPPING
    if (lightStruct == selectedLight && selectedGameObjectType == "light" && !inGamePreview) return;
#endif

    if (lightStruct->parent != this || !lightStruct->parent->getFlag(Entity::Flag::INITIALIZED)) lightStruct->parent = this;

    lightStruct->light.position = glm::vec3(this->position.x, this->position.y, this->position.z) + lightStruct->lightInfo.relativePosition;
}

void Entity::makeChildrenInstances() {
    // for (Entity* entity : entitiesChildren) {
    //     addInstance(entity);
    //     entity->makeChildrenInstances();
    // }
}

void Entity::setName(const std::string& newName) {
    name = newName;
}

std::string Entity::getName() const {
    return name;
}

void Entity::initializeDefaultModel() {
    Mesh mesh = GenMeshCube(1, 1, 1);
    model = LoadModelFromMesh(mesh);
    if (entityShader == nullptr)
        model.materials[0].shader = *shaderManager.m_defaultShader;
    else
        model.materials[0].shader = *entityShader;
}

void Entity::loadModel(const char* filename, const char* textureFilename) {
    model = LoadModel(filename);
}

void Entity::UpdateTextureMap(const int& mapType, const SurfaceMaterialTexture& texture, const bool& lodEnabled) {
    auto& targetTexture = model.materials[0].maps[mapType].texture;
    SurfaceMaterialTexture mutableTexture;

    switch (texture.activatedMode) {
        case 0:
            targetTexture = texture.getTexture2D();
            break;
        case 1:
            mutableTexture = texture;
            mutableTexture.updateVideo();
            targetTexture = mutableTexture.getVideoTexture();
            break;
        default:
            targetTexture = { 0 };
            break;
    }

    if (lodEnabled) {
        for (auto& lodModel : LodModels) {
            if (IsModelReady(lodModel)) {
                lodModel.materials[0].maps[mapType].texture = targetTexture;
            }
        }
    }
}

void Entity::ReloadTextures(const bool& force_reload) {
    auto updateIfNeeded = [&](int mapType, const SurfaceMaterialTexture& texture, bool condition) {
        if (condition || force_reload) {
            UpdateTextureMap(mapType, texture, this->getFlag(Entity::Flag::LOD_ENABLED));
        }
    };

    updateIfNeeded(MATERIAL_MAP_DIFFUSE,   surfaceMaterial.albedoTexture,     !surfaceMaterial.albedoTexturePath.empty());
    updateIfNeeded(MATERIAL_MAP_NORMAL,    surfaceMaterial.normalTexture,     !surfaceMaterial.normalTexturePath.empty());
    updateIfNeeded(MATERIAL_MAP_ROUGHNESS, surfaceMaterial.roughnessTexture,  !surfaceMaterial.roughnessTexturePath.empty());
    updateIfNeeded(MATERIAL_MAP_OCCLUSION, surfaceMaterial.aoTexture,         !surfaceMaterial.aoTexturePath.empty());
    updateIfNeeded(MATERIAL_MAP_HEIGHT,    surfaceMaterial.heightTexture,     !surfaceMaterial.heightTexturePath.empty());
    updateIfNeeded(MATERIAL_MAP_METALNESS, surfaceMaterial.metallicTexture,   !surfaceMaterial.metallicTexturePath.empty());
    updateIfNeeded(MATERIAL_MAP_EMISSION,  surfaceMaterial.emissiveTexture,   !surfaceMaterial.emissiveTexturePath.empty());
}


void Entity::OptimizeEntityMemory() {
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

void Entity::setModel(const fs::path& path, const Model& entityModel) {
    modelPath = path;
    model = modelPath.empty() ? entityModel : LoadModel(path.string().c_str());

    if (!IsModelReady(model)) {
        TraceLog(LOG_WARNING, "Could not set invalid model.");
        return;
    };

    setShader(entityShader);

    constBounds = GetMeshBoundingBox(model.meshes[0]);

    if (this->getFlag(Entity::Flag::LOD_ENABLED) && model.meshes[0].vertexCount > 48) {
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

        OptimizedMeshData data(indices, vertices);

        this->LodModels[0] = this->model;

        data = OptimizeMesh(indices, vertices, 0.8f);
        this->LodModels[1] = LoadModelFromMesh(generateLODMesh(data.vertices, data.indices, model.meshes[0]));

        data = OptimizeMesh(indices, vertices, 0.6f);
        this->LodModels[2] = LoadModelFromMesh(generateLODMesh(data.vertices, data.indices, model.meshes[0]));

        data = OptimizeMesh(indices, vertices, 0.3f);
        this->LodModels[3] = LoadModelFromMesh(generateLODMesh(data.vertices, data.indices, model.meshes[0]));

        OptimizeEntityMemory();
    }

    this->getFlag(Entity::Flag::IS_DYNAMIC) ? makePhysicsDynamic() : makePhysicsStatic();

    ReloadTextures();
}

bool Entity::hasModel() {
    return IsModelReady(model);
}

void Entity::setShader(std::shared_ptr<Shader> newShader) {
    if (!newShader || !IsShaderReady(*newShader)) {
        TraceLog(LOG_ERROR, "Shader is invalid! Could not set shader to entity.");
        if (shaderManager.m_defaultShader && IsShaderReady(*shaderManager.m_defaultShader) && IsModelReady(model)) {
            entityShader = shaderManager.m_defaultShader;
            model.materials[0].shader = *shaderManager.m_defaultShader;
            TraceLog(LOG_INFO, "Using default shader for entity.");
        } else {
            TraceLog(LOG_ERROR, "Default shader is also invalid or model not ready!");
        }
        return;
    }\

    entityShader = newShader;
    if (IsModelReady(model)) {
        model.materials[0].shader = *newShader;
    }

    for (int index = 0; index < 4; index++) {
        if (IsModelReady(LodModels[index])) {
            LodModels[index].materials[0].shader = *newShader;
        }
    }
}

std::shared_ptr<Shader> Entity::getShader() {
    if (!entityShader.get()) {
        TraceLog(LOG_WARNING, "Shader is null, returning default shader.");
        return shaderManager.m_defaultShader;
    }

    return entityShader;
}

void Entity::initializeSharedModules() {
    inputModule     = py::module::import("inputModule");
    collisionModule = py::module::import("collisionModule");
    cameraModule    = py::module::import("cameraModule");
    physicsModule   = py::module::import("physicsModule");
    mouseModule     = py::module::import("mouseModule");
    timeModule      = py::module::import("timeModule");
    colorModule     = py::module::import("colorModule");
    mathModule      = py::module::import("mathModule");
    eventsModule    = py::module::import("eventsModule");
    py::module_::import("__main__").attr("entitiesList") = py::cast(entitiesList);
}

void Entity::setupScript(LitCamera* rendering_camera) {
    if (scriptPath.empty()) return;
    this->setFlag(Entity::Flag::RUNNING, true);

    if (!py::hasattr(entityModule, "Entity")) {
        py::class_<Entity>(entityModule, "Entity")
            .def(py::init([](py::args args, py::kwargs kwargs) {
                LitVector3 position{0, 0, 0};
                std::string modelPath = "";

                if (args.size() > 0)               position = py::cast<LitVector3>(args[0]);
                if (kwargs.contains("modelPath"))  modelPath = py::cast<std::string>(kwargs["modelPath"]);

                Entity entity;

                if (kwargs.contains("scale")) entity.setScale(py::cast<LitVector3>(kwargs["scale"]));
                else entity.setScale(LitVector3{1, 1, 1});

                if (kwargs.contains("name")) entity.setName(py::cast<std::string>(kwargs["name"]));
                else entity.setName("New Entity");

                if (kwargs.contains("collisions"))   entity.setFlag(Entity::Flag::COLLIDER, py::cast<bool>(kwargs["collisions"]));
                if (kwargs.contains("collider"))     entity.currentCollisionShapeType = py::cast<CollisionShapeType>(kwargs["collider"]);
                if (!modelPath.empty()) entity.setModel(modelPath.c_str());

                entity.setPos(position);
                entitiesListPregame.emplace_back(entity);

                return entitiesListPregame.back();
            }))

            .def_property("name", &Entity::getName, &Entity::setName)
            .def_property("position",
                [](Entity& entity) { return entity.position; },
                [](Entity& entity, LitVector3& position) { entity.setPos(position); })
            .def_property("scale",
                [](Entity& entity) { return entity.scale; },
                [](Entity& entity, LitVector3& scale) { entity.setScale(scale); })
            .def_property("rotation",
                [](Entity& entity) { return entity.rotation; },
            [](Entity& entity, LitVector3& rotation) { entity.setRot(rotation); })
            .def_property("visible",
                [](Entity& entity) { return entity.getFlag(Entity::Flag::VISIBLE); },
                [](Entity& entity, bool& visible) { entity.setFlag(Entity::Flag::VISIBLE, visible); })
            .def_property("collision",
                [](Entity& entity) { return entity.getFlag(Entity::Flag::COLLIDER); },
                [](Entity& entity, bool& collision) { entity.setFlag(Entity::Flag::COLLIDER, collision); })
            .def_readwrite("collider", &Entity::currentCollisionShapeType)
            .def_readwrite("id", &Entity::id)
            .def("setLinearVelocity", &Entity::setLinearVelocity)
            .def("applyForce", &Entity::applyForce)
            .def("applyImpulse", &Entity::applyImpulse)
            .def("setFriction", &Entity::setFriction)
            .def("makeStatic", &Entity::makePhysicsStatic)
            .def("makeDynamic", &Entity::makePhysicsDynamic);
    }

    initializeSharedModules();

    try {
        localNamespace = py::dict();

        localNamespace["entity"] = py::cast(this);
        localNamespace["IsMouseButtonPressed"] = inputModule.attr("isMouseButtonPressed");
        localNamespace["IsKeyDown"] = inputModule.attr("isKeyDown");
        localNamespace["IsKeyPressed"] = inputModule.attr("isKeyPressed");
        localNamespace["IsKeyUp"] = inputModule.attr("isKeyUp");
        localNamespace["GetMouseMovement"] = inputModule.attr("getMouseMovement");
        localNamespace["Key"] = inputModule.attr("Key");
        localNamespace["MouseButton"] = inputModule.attr("MouseButton");
        localNamespace["Raycast"] = collisionModule.attr("raycast");
        localNamespace["CollisionShape"] = collisionModule.attr("CollisionShape");
        localNamespace["Vector3"] = mathModule.attr("Vector3");
        localNamespace["Vector2"] = mathModule.attr("Vector2");
        localNamespace["Vector3Scale"] = mathModule.attr("vector3Scale");
        localNamespace["Vector3Distance"] = mathModule.attr("vector3Distance");
        localNamespace["Vector3Length"] = mathModule.attr("vector3Length");
        localNamespace["Vector3LengthSqr"] = mathModule.attr("vector3LengthSqr");
        localNamespace["Color"] = colorModule.attr("Color");
        localNamespace["LockMouse"] = mouseModule.attr("LockMouse");
        localNamespace["UnlockMouse"] = mouseModule.attr("UnlockMouse");
        localNamespace["time"] = py::cast(&timeInstance, py::return_value_policy::reference);
        localNamespace["physics"] = py::cast(&physics);
        localNamespace["Lerp"] = mathModule.attr("lerp");
        localNamespace["Clamp"] = mathModule.attr("clamp");
        localNamespace["entitiesList"] = entitiesList;
        localNamespace["camera"] = py::cast(rendering_camera);
        localNamespace["onEntityCreation"] = eventsModule.attr("onEntityCreation");
        localNamespace["onEntityDestruction"] = eventsModule.attr("onEntityDestruction");
        localNamespace["createEvent"] = eventsModule.attr("createEvent");
        localNamespace["onCustomEvent"] = eventsModule.attr("onCustomEvent");
        localNamespace["triggerCustomEvent"] = eventsModule.attr("triggerCustomEvent");
        localNamespace["Entity"] = entityModule.attr("Entity");

        // Load and execute the script in its own namespace
        #ifndef GAME_SHIPPING
            scriptContent = readFileToString(scriptPath);
        #else
            std::ifstream infile("encryptedScripts.json");
            if (!infile.is_open()) {
                TraceLog(LOG_ERROR, "Failed to open scripts file.");
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
                        }
                    }
                }
            } else {
                return;
            }
        #endif

        py::exec(scriptContent, localNamespace);
    } catch (const py::error_already_set& e) {
        py::print("Error in setupScript for entity ", id, ": ", e.what());
    }
}

void Entity::runScript(LitCamera* rendering_camera) {
    if (scriptPath.empty() || scriptContent.empty()) return;

    try {
        if (py::hasattr(localNamespace["update"], "__call__")) {
            py::object update_func = localNamespace["update"];
            update_func();
            rendering_camera->update();
        }
    } catch (const py::error_already_set& e) {
        TraceLog(LOG_ERROR, (std::string("Failed to run script for entity ") +
                std::to_string(id) + ": " + std::string(e.what())).c_str());
    }
}

void Entity::calcPhysicsPosition() {
    if (!this->getFlag(Entity::Flag::IS_DYNAMIC)) return;

    if (currentCollisionShapeType == CollisionShapeType::Box || currentCollisionShapeType == CollisionShapeType::HighPolyMesh) {
        btTransform trans;
        if (rigidBody && rigidBody->getMotionState()) {
            rigidBody->getMotionState()->getWorldTransform(trans);
            btVector3 rigidBodyPosition = trans.getOrigin();
            position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
        }
    }
}

void Entity::calcPhysicsRotation() {
    if (!this->getFlag(Entity::Flag::IS_DYNAMIC)) return;

    if (currentCollisionShapeType == CollisionShapeType::Box && rigidBody) {
        btTransform trans;
        if (rigidBody->getMotionState()) {
            rigidBody->getMotionState()->getWorldTransform(trans);
            btQuaternion objectRotation = trans.getRotation();
            btScalar roll, yaw, pitch;
            objectRotation.getEulerZYX(roll, yaw, pitch);

            rotation = Vector3{ pitch * RAD2DEG, yaw * RAD2DEG, roll * RAD2DEG };
        }
    }
}

void Entity::setPos(const LitVector3& newPos) {
    position = newPos;

    if (rigidBody) {
        btTransform transform;
        transform.setIdentity();
        transform.setOrigin(btVector3(newPos.x, newPos.y, newPos.z));

        rigidBody->setWorldTransform(transform);
    }
}

void Entity::setRot(const LitVector3& newRot) {
    rotation = newRot;

    if (CollisionShapeType::Box == currentCollisionShapeType) {
        if (rigidBody) {
            btTransform currentTransform = rigidBody->getWorldTransform();


            float rollRad = glm::radians(rotation.x);
            float pitchRad = glm::radians(rotation.y);
            float yawRad = glm::radians(rotation.z);

            btQuaternion newRotation;
            newRotation.setEulerZYX(yawRad, pitchRad, rollRad);

            currentTransform.setRotation(newRotation);
            rigidBody->setWorldTransform(currentTransform);
        }
    }
}

void Entity::setScale(const LitVector3& newScale) {
    scale = newScale;

    if (CollisionShapeType::Box == currentCollisionShapeType) {
        this->getFlag(Entity::Flag::IS_DYNAMIC) ? createDynamicBox() : createStaticBox();
    }

    else if (CollisionShapeType::HighPolyMesh == currentCollisionShapeType && this->getFlag(Entity::Flag::RUNNING)) {
        this->getFlag(Entity::Flag::IS_DYNAMIC) ? createDynamicMesh(false) : createStaticMesh(true);
    }
}

void Entity::setLinearVelocity(const LitVector3& velocity) {
    if (rigidBody && this->getFlag(Entity::Flag::IS_DYNAMIC)) {
        rigidBody->setActivationState(ACTIVE_TAG);
        btVector3 btVelocity(velocity.x, velocity.y, velocity.z);
        rigidBody->setLinearVelocity(btVelocity);
    }
}

void Entity::applyForce(const LitVector3& force) {
    if (rigidBody && this->getFlag(Entity::Flag::IS_DYNAMIC)) {
        rigidBody->setActivationState(ACTIVE_TAG);
        btVector3 btForce(force.x, force.y, force.z);
        rigidBody->applyCentralForce(btForce);
    }
}

void Entity::applyImpulse(const LitVector3& impulse) {
    if (rigidBody && this->getFlag(Entity::Flag::IS_DYNAMIC)) {
        rigidBody->setActivationState(ACTIVE_TAG);
        btVector3 btImpulse(impulse.x, impulse.y, impulse.z);
        rigidBody->applyCentralImpulse(btImpulse);
    }
}

void Entity::setFriction(const float& friction) {
    if (rigidBody && this->getFlag(Entity::Flag::IS_DYNAMIC)) {
        rigidBody->setFriction(friction);
    }
}

void Entity::applyDamping(const float& damping) {
    if (rigidBody && this->getFlag(Entity::Flag::IS_DYNAMIC)) {
        rigidBody->setDamping(damping, damping);
    }
}

void Entity::updateMass() {
    if (!this->getFlag(Entity::Flag::IS_DYNAMIC) || rigidShape == nullptr) return;

    btScalar btMass = mass;
    btVector3 boxInertia(inertia.x, inertia.y, inertia.z);
    rigidShape->calculateLocalInertia(btMass, boxInertia);
    if (currentCollisionShapeType == CollisionShapeType::Box && rigidBody && rigidBody != nullptr)
        rigidBody->setMassProps(btMass, boxInertia);
    else if (currentCollisionShapeType == CollisionShapeType::HighPolyMesh && rigidBody && rigidBody != nullptr)
        rigidBody->setMassProps(btMass, boxInertia);
}

void Entity::createStaticBox() {
    this->setFlag(Entity::Flag::IS_DYNAMIC, false);

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

    currentCollisionShapeType = CollisionShapeType::Box;
}

void Entity::createStaticMesh(const bool& generateShape) {
    this->setFlag(Entity::Flag::IS_DYNAMIC, false);

    // Remove the existing rigid body if it exists
    if (rigidBody) {
        physics.dynamicsWorld->removeRigidBody(rigidBody.get());
    }

    // Generate the collision shape if required or if it doesn't exist
    if (generateShape || !customMeshShape) {
        customMeshShape = std::make_shared<btConvexHullShape>();

        for (int m = 0; m < model.meshCount; m++) {
            Mesh& mesh = model.meshes[m];
            float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

            // Add each vertex to the convex hull shape
            for (int v = 0; v < mesh.vertexCount; v++) {
                btVector3 scaledVertex(
                    meshVertices[v * 3]     * scale.x,
                    meshVertices[v * 3 + 1] * scale.y,
                    meshVertices[v * 3 + 2] * scale.z
                );
                customMeshShape->addPoint(scaledVertex);
            }
        }
    }

    // Set the transform for the rigid body
    btTransform rigidTransform;
    rigidTransform.setIdentity();
    rigidTransform.setOrigin(btVector3(position.x, position.y, position.z));

    // Mass and inertia for a static object (mass = 0)
    btScalar rigidMass = 0.0f;
    btVector3 rigidInertia(0, 0, 0);
    customMeshShape->calculateLocalInertia(rigidMass, rigidInertia);

    // Create the motion state and rigid body construction info
    btDefaultMotionState boxMotionState = btDefaultMotionState(rigidTransform);
    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(rigidMass, &boxMotionState, customMeshShape.get(), rigidInertia);

    // Create the rigid body and add it to the physics world
    rigidBody = std::make_shared<btRigidBody>(rigidBodyCI);
    physics.dynamicsWorld->addRigidBody(rigidBody.get());

    // Update the current collision shape type
    currentCollisionShapeType = CollisionShapeType::HighPolyMesh;
}

void Entity::createDynamicBox() {
    this->setFlag(Entity::Flag::IS_DYNAMIC, true);

    if (rigidBody) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

    rigidShape = std::make_shared<btBoxShape>(btVector3(scale.x * scaleFactorRaylibBullet, scale.y * scaleFactorRaylibBullet, scale.z * scaleFactorRaylibBullet));

    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(btVector3(position.x, position.y, position.z));

    btScalar btMass = mass;
    btVector3 localInertia(inertia.x, inertia.y, inertia.z);
    rigidShape->calculateLocalInertia(btMass, localInertia);

    static btDefaultMotionState boxMotionState = btDefaultMotionState(startTransform);
    btRigidBody::btRigidBodyConstructionInfo rigidBodyCI(btMass, &boxMotionState, rigidShape.get(), localInertia);
    rigidBody = std::make_shared<btRigidBody>(rigidBodyCI);

    physics.dynamicsWorld->addRigidBody(rigidBody.get());
    currentCollisionShapeType = CollisionShapeType::Box;
}

void Entity::createDynamicMesh(const bool& generateShape) {
    this->setFlag(Entity::Flag::IS_DYNAMIC, true);

    currentCollisionShapeType = CollisionShapeType::HighPolyMesh;
    if (rigidBody.get()) physics.dynamicsWorld->removeRigidBody(rigidBody.get());

    if (generateShape || !customMeshShape) {
        customMeshShape = std::make_shared<btConvexHullShape>();

        for (int m = 0; m < model.meshCount; m++) {
            Mesh& mesh = model.meshes[m];
            float* meshVertices = reinterpret_cast<float*>(mesh.vertices);

            // Add each vertex to the convex hull shape
            for (int v = 0; v < mesh.vertexCount; v++) {
                btVector3 scaledVertex(
                    meshVertices[v * 3]     * scale.x,
                    meshVertices[v * 3 + 1] * scale.y,
                    meshVertices[v * 3 + 2] * scale.z
                );
                customMeshShape->addPoint(scaledVertex);
            }
        }
    }

    btTransform rigidTransform;
    rigidTransform.setIdentity();
    rigidTransform.setOrigin(btVector3(position.x, position.y, position.z));

    btScalar rigidMass = mass;
    btVector3 rigidInertia(0, 0, 0);
    customMeshShape.get()->calculateLocalInertia(rigidMass, rigidInertia);
    btDefaultMotionState boxMotionState = btDefaultMotionState(rigidTransform);
    btRigidBody::btRigidBodyConstructionInfo highPolyStaticRigidBodyCI(rigidMass, &boxMotionState, customMeshShape.get(), rigidInertia);

    rigidBody = std::make_shared<btRigidBody>(highPolyStaticRigidBodyCI);
    physics.dynamicsWorld->addRigidBody(rigidBody.get());
}

void Entity::makePhysicsDynamic(const CollisionShapeType& shapeType) {
    this->setFlag(Entity::Flag::IS_DYNAMIC, true);

    if (shapeType == CollisionShapeType::Box)                createDynamicBox();
    else if (shapeType == CollisionShapeType::HighPolyMesh)  createDynamicMesh();
}

void Entity::makePhysicsStatic(const CollisionShapeType& shapeType) {
    this->setFlag(Entity::Flag::IS_DYNAMIC, false);

    if (shapeType == CollisionShapeType::Box)                createStaticBox();
    else if (shapeType == CollisionShapeType::HighPolyMesh)  createStaticMesh();
}

void Entity::reloadRigidBody() {
    getFlag(Entity::Flag::IS_DYNAMIC) ? makePhysicsDynamic(currentCollisionShapeType) : makePhysicsStatic(currentCollisionShapeType);
}

void Entity::resetPhysics() {
    if (rigidBody && rigidBody != nullptr) {
        rigidBody->setLinearVelocity(btVector3(0, 0, 0));
        rigidBody->setAngularVelocity(btVector3(0, 0, 0));

        setPos(backupPosition);
    }
}

bool Entity::inFrustum() {
    return AABBoxInFrustum(bounds.min, bounds.max);
}

void Entity::render() {
    if (!getFlag(Entity::Flag::INITIALIZED)) return;

    if (!hasModel()) {
        initializeDefaultModel();
    }

    updateChildren();

    if (getFlag(Entity::Flag::CALC_PHYSICS)) {
        if (currentCollisionShapeType != CollisionShapeType::None && getFlag(Entity::Flag::IS_DYNAMIC)) {
            calcPhysicsRotation();
            calcPhysicsPosition();
        }
        ReloadTextures();
    } else {
        backupPosition = position;
        setPos(position);
        setRot(rotation);
        setScale(scale);
        updateMass();
        ReloadTextures(true);
    }

    if (!getFlag(Entity::Flag::VISIBLE)) return;

    instances.empty() ? renderSingleModel() : renderInstanced();
}

void Entity::setFlag(const Flag& f, const bool& value) {
    flags.set(f, value);
}

bool Entity::getFlag(const Flag& f) const {
    return flags.test(f);
}


void Entity::renderInstanced() {
    PassSurfaceMaterials();

    glUseProgram((GLuint)(*shaderManager.m_instancingShader).id);

    matInstances = LoadMaterialDefault();

    (*shaderManager.m_instancingShader).locs[SHADER_LOC_MATRIX_MVP] = shaderManager.GetUniformLocation((*shaderManager.m_instancingShader).id, "mvp");
    (*shaderManager.m_instancingShader).locs[SHADER_LOC_VECTOR_VIEW] = shaderManager.GetUniformLocation((*shaderManager.m_instancingShader).id, "viewPos");
    (*shaderManager.m_instancingShader).locs[SHADER_LOC_MATRIX_MODEL] = shaderManager.GetAttribLocation((*shaderManager.m_instancingShader).id, "instanceTransform");

    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = RAYWHITE;

    transforms.resize(instances.size());
    DrawMeshInstanced(model.meshes[0], model.materials[0], transforms.data(), instances.size());
}

void Entity::renderSingleModel() {
    if (!hasModel()) {
        return;
    }

    const Matrix scaleMatrix = MatrixScale(scale.x, scale.y, scale.z);
    const Matrix rotationMatrix = MatrixRotateXYZ(Vector3Scale(rotation, DEG2RAD));
    const Matrix translationMatrix = MatrixTranslate(position.x, position.y, position.z);

    const Matrix transformMatrix = MatrixMultiply(MatrixMultiply(scaleMatrix, rotationMatrix), translationMatrix);

    model.transform = transformMatrix;

    if (model.meshes != nullptr) {
        bounds.min = Vector3Transform(constBounds.min, transformMatrix);
        bounds.max = Vector3Transform(constBounds.max, transformMatrix);
    }

    if (!inFrustum()) return;

    PassSurfaceMaterials();
    glUseProgram((GLuint)entityShader->id);

    float distance;
#ifndef GAME_SHIPPING
    distance = inGamePreview ? Vector3Distance(this->position, camera.position)
                            : Vector3Distance(this->position, sceneCamera.position);
#else
    distance = Vector3Distance(this->position, camera.position);
#endif

    Model& modelToDraw = model;
    if (this->getFlag(Entity::Flag::LOD_ENABLED)) {
        const int lodLevel = (distance < LOD_DISTANCE_HIGH) ? 0
                        : (distance < LOD_DISTANCE_MEDIUM) ? 1
                        : (distance < LOD_DISTANCE_LOW) ? 2
                        : 3;

        for (Model& lodModel : LodModels) {
            lodModel.transform = transformMatrix;
        }

        if (IsModelReady(LodModels[lodLevel])) modelToDraw = LodModels[lodLevel];
    }

    DrawModel(modelToDraw, Vector3Zero(), 1, RAYWHITE);
}

void Entity::PassSurfaceMaterials() {
    glUseProgram(entityShader->id);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skybox.irradianceTex.id);

    constexpr int irrUnit = 8;
    SetShaderValue(*entityShader,
        shaderManager.GetUniformLocation(entityShader->id, "irradiance"),
        &irrUnit, SHADER_UNIFORM_INT);
}


bool operator==(const Entity& e, const Entity* ptr) {
    return &e == ptr;
}

void removeEntity(const int& id) {
    auto it = entityIdToIndexMap.find(id);
    if (it != entityIdToIndexMap.end()) {
        eventManager.onEntityDestruction.triggerEvent();
        size_t index = it->second;
        entityIdToIndexMap.erase(it);

#ifndef GAME_SHIPPING
        if (index != entitiesListPregame.size() - 1) {
            std::swap(entitiesListPregame[index], entitiesListPregame.back());
            entityIdToIndexMap[entitiesListPregame[index].id] = index;
        }
        entitiesListPregame.pop_back();
#else
        if (index != entitiesList.size() - 1) {
            std::swap(entitiesList[index], entitiesList.back());
            entityIdToIndexMap[entitiesList[index].id] = index;
        }
        entitiesList.pop_back();
#endif
    }
}

int getIdFromEntity(const Entity& entity) {
#ifndef GAME_SHIPPING
    auto it = std::find(entitiesListPregame.begin(), entitiesListPregame.end(), entity);
    if (it != entitiesListPregame.end()) {
        return it->id;
    }
#else
    auto it = std::find(entitiesList.begin(), entitiesList.end(), entity);
    if (it != entitiesList.end()) {
        return it->id;
    }
#endif
    return -1;
}

Entity* getEntityById(const int& id) {
    auto it = entityIdToIndexMap.find(id);
    if (it != entityIdToIndexMap.end()) {
#ifndef GAME_SHIPPING
        return &entitiesListPregame[it->second];
#else
        return &entitiesList[it->second];
#endif
    }
    return nullptr;
}

void AddEntity(
    const fs::path& modelPath,
    const Model& model,
    const std::string& name,
    const int& id
) {
    eventManager.onEntityCreation.triggerEvent();

    Entity entityCreate;
    entityCreate.setScale(Vector3{1, 1, 1});
    entityCreate.setName(name);
    entityCreate.setModel(modelPath, model);
    entityCreate.setShader(shaderManager.m_defaultShader);

    #ifndef GAME_SHIPPING
    if (id == -1) entityCreate.id = entitiesListPregame.size() + lights.size();
    else          entityCreate.id = id;
    entityIdToIndexMap[entityCreate.id] = entitiesListPregame.size();

    entitiesListPregame.emplace_back(std::move(entityCreate));
    selectedGameObjectType = "entity";
    selectedEntity = &entitiesListPregame.back();
#else
    if (id == -1) entityCreate.id = entitiesList.size() + lights.size();
    else          entityCreate.id = id;
    entityIdToIndexMap[entityCreate.id] = entitiesList.size();

    entitiesList.emplace_back(std::move(entityCreate));
#endif
}
