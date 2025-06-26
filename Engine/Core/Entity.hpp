/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef ENTITY_H
#define ENTITY_H

struct LitCamera;

#include <Engine/Core/Macros.hpp>
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
#include <bitset>

namespace fs = std::filesystem;
namespace py = pybind11;

struct HitInfo;
struct LightStruct;

py::module createEntityModule();
extern py::module entityModule;

enum ObjectTypeEnum {
  ObjectType_None,
  ObjectType_Cube,
  ObjectType_Cone,
  ObjectType_Cylinder,
  ObjectType_Plane,
  ObjectType_Sphere,
  ObjectType_Torus
};

class EXPORT_API Entity {
  public:
    int id = 0;
    ObjectTypeEnum ObjectType;
    CollisionShapeType currentCollisionShapeType = CollisionShapeType::None;

    // collider, visible, isChild, isParent, running, runningFirstTime, calcPhysics, isDynamic, lodEnabled
    std::bitset<16> flags;

    enum Flag {
      COLLIDER = 0,
      VISIBLE,
      IS_CHILD,
      IS_PARENT,
      RUNNING,
      RUNNING_FIRST_TIME,
      CALC_PHYSICS,
      IS_DYNAMIC,
      LOD_ENABLED,
      INITIALIZED
    };

    float size = 1;
    float mass = 1;
    float friction = 1;
    float damping = 0;

    LitVector3 backupPosition = {0, 0, 0};
    LitVector3 position = {0, 0, 0};
    LitVector3 rotation = {0, 0, 0};
    LitVector3 scale    = {1, 1, 1};

    LitVector3 relativePosition = {0, 0, 0};
    LitVector3 relativeRotation = {0, 0, 0};
    LitVector3 relativeScale = {1, 1, 1};

    LitVector3 inertia = {0, 0, 0};

    BoundingBox bounds;
    BoundingBox constBounds;

    std::string name = "Entity";
    std::string scriptIndex;

    fs::path scriptPath;
    fs::path modelPath;
    fs::path childMaterialPath;

    Model model;
    Model LodModels[4] = {};
    SurfaceMaterial surfaceMaterial;

    Entity* parent = nullptr;

    std::vector<int> lightsChildren;
    std::vector<int> entitiesChildren;
    std::vector<Entity*> instances;

  private:
    std::shared_ptr<btCollisionShape> rigidShape;
    std::shared_ptr<btConvexHullShape> customMeshShape;
    std::shared_ptr<btRigidBody> rigidBody;

    std::vector<Matrix> transforms;
    Material matInstances;

    std::shared_ptr<Shader> entityShader = shaderManager.m_defaultShader;

    std::string scriptContent;
    py::dict localNamespace;

    bool pythonModulesInitialized;
    py::module inputModule, collisionModule, cameraModule, physicsModule,
        mouseModule, timeModule, colorModule, mathModule, eventsModule, engineModule;

    bool entityOptimized = false;

  public:
    Entity(std::string name = "entity", LitVector3 position = {0, 0, 0},
           LitVector3 scale = {1, 1, 1}, LitVector3 rotation = {0, 0, 0})
        : name(name), position(position), scale(scale), rotation(rotation) {
          this->setFlag(Flag::INITIALIZED, true);
          this->setFlag(Flag::VISIBLE, true);
    }

    bool operator==(const Entity& other) const { return this->id == other.id; }

    void addInstance(Entity* instance);
    bool hasInstances() const;
    void calculateInstance();
    void addEntityChild(const int& id);
    void removeEntityChild(const int& id);
    void addLightChild(const int& newLightIndex);
    void updateChildren();
    void updateEntityChild(Entity* entity, const int& entityIndex);
    void updateLightChild(LightStruct* lightStruct, const int& lightIndex);
    void makeChildrenInstances();
    void setName(const std::string& newName);
    std::string getName() const;
    void initializeDefaultModel();
    void loadModel(const char* filename, const char* textureFilename = NULL);
    void UpdateTextureMap(const int& mapType, const SurfaceMaterialTexture& texture,
                          const bool& lodEnabled);
    void ReloadTextures(const bool& force_reload = false);
    void OptimizeEntityMemory();
    void setModel(const fs::path& path = "",
                  const Model& entityModel = Model());
    bool hasModel();
    void setShader(std::shared_ptr<Shader> newShader);
    std::shared_ptr<Shader> getShader();
    void initializeSharedModules();
    void setupScript(LitCamera* rendering_camera);
    void runScript(LitCamera* rendering_camera);
    void calcPhysicsPosition();
    void calcPhysicsRotation();
    void setPos(const LitVector3& newPos);
    void setRot(const LitVector3& newRot);
    void setLinearVelocity(const LitVector3& velocity);
    void setScale(const LitVector3& newScale);
    void applyForce(const LitVector3& force);
    void applyImpulse(const LitVector3& impulse);
    void setFriction(const float& friction);
    void applyDamping(const float& damping);
    void updateMass();
    void createStaticBox();
    void createStaticMesh(const bool& generateShape = true);
    void createDynamicBox();
    void createDynamicMesh(const bool& generateShape = true);
    void makePhysicsDynamic(const CollisionShapeType& shapeType = CollisionShapeType::Box);
    void makePhysicsStatic(const CollisionShapeType& shapeType = CollisionShapeType::None);
    void reloadRigidBody();
    void resetPhysics();
    bool inFrustum();
    void render();
    void setFlag(const Flag& f, const bool& value);
    bool getFlag(const Flag& f) const;

  private:
    void renderInstanced();
    void renderSingleModel();
    void PassSurfaceMaterials();
};

struct EntityHandle {
    int id;
    EntityHandle(int entityId) : id(entityId) {}
    Entity* get() const;
};

void InitFrustum();
void UpdateFrustum();
bool PointInFrustum(const Vector3& point);
bool SphereInFrustum(const Vector3& position, const float& radius);
bool AABBoxInFrustum(const Vector3& min, const Vector3& max);
void removeEntity(const int& id);
Entity* getEntityById(const int& id);
int getIdFromEntity(const Entity& entity);
void AddEntity(const fs::path& modelPath = "",
                  const Model& model = LoadModelFromMesh(GenMeshCube(1, 1, 1)),
                  const std::string& name = "Unnamed Entity",
                  const int& id = -1);

#endif // ENTITY_H