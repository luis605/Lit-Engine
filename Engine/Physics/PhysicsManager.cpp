float scaleFactorRaylibBullet = 0.5f;

class PhysicsManager
{
public:
    btDbvtBroadphase* broadphase;
    btDefaultCollisionConfiguration* collisionConfiguration;
    btCollisionDispatcher* dispatcher;
    btSequentialImpulseConstraintSolver* solver;
    btDiscreteDynamicsWorld* dynamicsWorld;

    LitVector3 gravity;
    LitVector3 backupGravity;

public:
    PhysicsManager()
    {
        Init();
    }

    void Init()
    {
        broadphase = new btDbvtBroadphase();
        collisionConfiguration = new btDefaultCollisionConfiguration();
        dispatcher = new btCollisionDispatcher(collisionConfiguration);
        solver = new btSequentialImpulseConstraintSolver();
        dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
        dynamicsWorld->setGravity(btVector3(0, -9.81, 0));
        gravity.x = dynamicsWorld->getGravity().x();
        gravity.y = dynamicsWorld->getGravity().y();
        gravity.z = dynamicsWorld->getGravity().z();
    }

    void Update(float deltaTime)
    {
        dynamicsWorld->stepSimulation(deltaTime, 10);
    }
    
    void setGravity(LitVector3 gravity)
    {
        dynamicsWorld->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
        this->gravity.x = dynamicsWorld->getGravity().x();
        this->gravity.y = dynamicsWorld->getGravity().y();
        this->gravity.z = dynamicsWorld->getGravity().z();
    }

    void backup()
    {
        this->backupGravity = this->gravity;
    }

    void unBackup()
    {
        this->setGravity(this->backupGravity);
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
