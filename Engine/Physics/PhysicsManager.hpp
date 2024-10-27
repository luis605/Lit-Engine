#ifndef PHYSICS_MANAGER_H
#define PHYSICS_MANAGER_H

float scaleFactorRaylibBullet = 0.5f;

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
    PhysicsManager() { init(); }

    void init();
    void update(float deltaTime);
    void setGravity(LitVector3 gravity);
    void backup();
    void unBackup();

    ~PhysicsManager() {
        TraceLog(LOG_INFO, "PhysicsManager: Unloading Physics Manager");
        delete dynamicsWorld;
        delete solver;
        delete dispatcher;
        delete collisionConfiguration;
        delete broadphase;
    }
};

PhysicsManager physics;

#endif // PHYSICS_MANAGER_H