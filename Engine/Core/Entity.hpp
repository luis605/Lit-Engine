#ifndef ENTITY_H
#define ENTITY_H

struct LitCamera;

#include <Engine/Lighting/Shaders.hpp>
#include <Engine/Lighting/SurfaceMaterial.hpp>
#include <Engine/Physics/PhysicsManager.hpp>
#include <Engine/Scripting/functions.hpp>
#include <Engine/Scripting/math.hpp>
#include <btBulletDynamicsCommon.h>
#include <filesystem>
#include <memory>
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace fs = std::filesystem;
namespace py = pybind11;

struct HitInfo;
struct LightStruct;

py::module createEntityModule();
extern bool Entity_already_registered;

class __attribute__((visibility("default"))) Entity {
  public:
    bool initialized = false;
    std::string name = "Entity";
    float size = 1;
    LitVector3 position = {0, 0, 0};
    LitVector3 rotation = {0, 0, 0};
    LitVector3 scale = {1, 1, 1};

    LitVector3 relativePosition = {0, 0, 0};
    LitVector3 relativeRotation = {0, 0, 0};
    LitVector3 relativeScale = {1, 1, 1};

    LitVector3 inertia = {0, 0, 0};

    std::string script;
    std::string scriptIndex;
    fs::path modelPath;

    Model model;
    Model LodModels[4] = {};

    BoundingBox bounds;
    BoundingBox constBounds;

    fs::path surfaceMaterialPath;
    SurfaceMaterial surfaceMaterial;

    float tiling[2] = {1.0f, 1.0f};
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

    CollisionShapeType currentCollisionShapeType = CollisionShapeType::None;

    int id = 0;

    Entity* parent = nullptr;

    std::vector<int> lightsChildren;   // Store the indices
    std::vector<int> entitiesChildren; // Store the indices

    std::vector<Entity*> instances;

  private:
    std::shared_ptr<btCollisionShape> rigidShape;
    std::shared_ptr<btConvexHullShape> customMeshShape;
    std::shared_ptr<btDefaultMotionState> boxMotionState;
    std::shared_ptr<btRigidBody> rigidBody;
    LitVector3 backupPosition = position;

    Matrix* transforms = nullptr;
    Material matInstances;

    Shader* entityShader = &shader;

    std::string scriptContent;
    py::module entityModule;

#pragma GCC visibility push(default)
    py::dict localNamespace;
    bool pythonModulesInitialized;
    py::module inputModule, collisionModule, cameraModule, physicsModule,
        mouseModule, timeModule, colorModule, mathModule;
#pragma GCC visibility pop

    bool entityOptimized = false;

  public:
    Entity(std::string name = "entity", LitVector3 position = {0, 0, 0},
           LitVector3 scale = {1, 1, 1}, LitVector3 rotation = {0, 0, 0})
        : name(name), position(position), scale(scale), rotation(rotation) {
        initialized = true;
        entityModule = createEntityModule();
    }

    bool operator==(const Entity& other) const { return this->id == other.id; }

    void addInstance(Entity* instance);
    bool hasInstances() const;
    void calculateInstance(int index);
    void addEntityChild(int newEntityIndex);
    void addLightChild(int newLightIndex);
    void updateChildren();
    void updateEntityChild(Entity* entity, int entityIndex);
    void updateLightChild(LightStruct* lightStruct, int lightIndex);
    void makeChildrenInstances();
    void setName(const std::string& newName);
    std::string getName() const;
    void initializeDefaultModel();
    void loadModel(const char* filename, const char* textureFilename = NULL);
    void UpdateTextureMap(int mapType, const SurfaceMaterialTexture& texture,
                          bool lodEnabled);
    void ReloadTextures(bool force_reload = false);
    void OptimizeEntityMemory();
    void setModel(const fs::path& path = "",
                  const Model& entityModel = Model());
    bool hasModel();
    void setShader(Shader& newShader);
    Shader& getShader();
    void initializeSharedModules();
    void setupScript(LitCamera* rendering_camera);
    void runScript(LitCamera* rendering_camera);
    void calcPhysicsPosition();
    void calcPhysicsRotation();
    void setPos(LitVector3 newPos);
    void setRot(LitVector3 newRot);
    void setScale(Vector3 newScale);
    void applyForce(const LitVector3& force);
    void applyImpulse(const LitVector3& impulse);
    void setFriction(const float& friction);
    void applyDamping(const float& damping);
    void updateMass();
    void createStaticBox();
    void createStaticMesh(bool generateShape = true);
    void createDynamicBox();
    void createDynamicMesh(bool generateShape = true);
    void
    makePhysicsDynamic(CollisionShapeType shapeType = CollisionShapeType::Box);
    void
    makePhysicsStatic(CollisionShapeType shapeType = CollisionShapeType::None);
    void reloadRigidBody();
    void resetPhysics();
    bool inFrustum();
    void render();

  private:
    void renderInstanced();
    void renderSingleModel();
    void PassSurfaceMaterials();
};

void InitFrustum();
void UpdateFrustum();
bool PointInFrustum(const Vector3& point);
bool SphereInFrustum(const Vector3& position, float radius);
bool AABBoxInFrustum(const Vector3& min, const Vector3& max);
void removeEntity(int id);
Entity* getEntityById(int id);
int getIdFromEntity(const Entity& entity);
Entity* AddEntity(const fs::path& modelPath = "",
                  const Model& model = LoadModelFromMesh(GenMeshCube(1, 1, 1)),
                  const std::string& name = "Unnamed Entity",
                  const int id = -1);

#endif // ENTITY_H