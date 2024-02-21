float scaleFactorRaylibBullet = 0.5f;

class PhysicsManager
{
public:
    btDbvtBroadphase* broadphase;
    btDefaultCollisionConfiguration* collisionConfiguration;
    btCollisionDispatcher* dispatcher;
    btSequentialImpulseConstraintSolver* solver;
    btDiscreteDynamicsWorld* dynamicsWorld;

    float gravity[3];

public:
    PhysicsManager()
    {
        broadphase = new btDbvtBroadphase();
        collisionConfiguration = new btDefaultCollisionConfiguration();
        dispatcher = new btCollisionDispatcher(collisionConfiguration);
        solver = new btSequentialImpulseConstraintSolver();
        dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
        dynamicsWorld->setGravity(btVector3(0, -9.81, 0));
        gravity[0] = dynamicsWorld->getGravity().x();
        gravity[1] = dynamicsWorld->getGravity().y();
        gravity[2] = dynamicsWorld->getGravity().z();
    }

    ~PhysicsManager()
    {
        delete dynamicsWorld;
        delete solver;
        delete dispatcher;
        delete collisionConfiguration;
        delete broadphase;
    }
};

PhysicsManager physics;
