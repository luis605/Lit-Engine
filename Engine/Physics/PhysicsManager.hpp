#ifndef PHYSICS_MANAGER_H
#define PHYSICS_MANAGER_H

#include <btBulletDynamicsCommon.h>
#include <Engine/Scripting/math.hpp>

extern float scaleFactorRaylibBullet;

enum CollisionShapeType {
    Box           = 0,
    HighPolyMesh  = 1,
    None          = 2
};

class PhysicsManager {
public:
    btDbvtBroadphase* broadphase;
    btDefaultCollisionConfiguration* collisionConfiguration;
    btCollisionDispatcher* dispatcher;
    btSequentialImpulseConstraintSolver* solver;
    btDiscreteDynamicsWorld* dynamicsWorld;

    LitVector3 gravity;
    LitVector3 backupGravity;

public:
    PhysicsManager() { Init(); }

    void Init();
    void Update(float deltaTime);
    void SetGravity(const LitVector3& gravity);
    void Backup();
    void UnBackup();

    ~PhysicsManager() {
        TraceLog(LOG_INFO, "PhysicsManager: Unloading Physics Manager");
        delete dynamicsWorld;
        delete solver;
        delete dispatcher;
        delete collisionConfiguration;
        delete broadphase;
    }
};

extern PhysicsManager physics;

#endif // PHYSICS_MANAGER_H