class Entity {
public:
    float size = 1;
    LitVector3 position = { 0, 0, 0 };
    LitVector3 rotation = { 0, 0, 0 };
    LitVector3 scale = { 1, 1, 1 };

    bool calc_physics = false;
    bool isDynamic = false;

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
    Vector3 inertia = {0, 0, 0};

    enum CollisionShapeType
    {
        Box           = 0,
        HighPolyMesh  = 1,
        LowPolyMesh   = 2,
        Sphere        = 3,
        None          = 4
    };

    CollisionShapeType currentCollisionShapeType;


private:
    btCollisionShape* staticBoxShape               = nullptr;
    btCollisionShape* dynamicBoxShape              = nullptr;
    btConvexHullShape* customMeshShape             = nullptr;
    btDefaultMotionState* boxMotionState           = nullptr;
    btTriangleMesh* triangleMesh                   = nullptr;
    btRigidBody* boxRigidBody                      = nullptr;
    btRigidBody* treeRigidBody                     = nullptr;
    LitVector3 backupPosition                      = position;

public:
    Entity(LitVector3 scale = { 1, 1, 1 }, LitVector3 rotation = { 0, 0, 0 },
    LitVector3 position = {0, 0, 0})
        : scale(scale), rotation(rotation), position(position)
    {   
        initialized = true;

    }

    void calcPhysicsPosition() {
        if (currentCollisionShapeType == Box && boxRigidBody) {
            btTransform trans;
            if (boxRigidBody->getMotionState()) {
                boxRigidBody->getMotionState()->getWorldTransform(trans);
                btVector3 rigidBodyPosition = trans.getOrigin();
                position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
            }
        }
        else if (currentCollisionShapeType == HighPolyMesh && treeRigidBody) {
            btTransform trans;
            if (treeRigidBody->getMotionState()) {
                treeRigidBody->getMotionState()->getWorldTransform(trans);
                btVector3 rigidBodyPosition = trans.getOrigin();
                position = { rigidBodyPosition.getX(), rigidBodyPosition.getY(), rigidBodyPosition.getZ() };
            }
        }
    }

    void calcPhysicsRotation() {
        if (!isDynamic) return;

        if (boxRigidBody) {
            btTransform trans;
            if (boxRigidBody->getMotionState()) {
                boxRigidBody->getMotionState()->getWorldTransform(trans);
                btQuaternion objectRotation = trans.getRotation();
                btScalar Roll, Yaw, Pitch;
                objectRotation.getEulerZYX(Roll, Yaw, Pitch);

                for (int index = 0; index < 4; index++)
                    LodModels[index].transform = MatrixRotateXYZ((Vector3){ Pitch, Yaw, Roll });
            }
        }
        else if (treeRigidBody) {
            btTransform trans;
            if (treeRigidBody->getMotionState()) {
                treeRigidBody->getMotionState()->getWorldTransform(trans);
                btQuaternion objectRotation = trans.getRotation();
                btScalar Roll, Yaw, Pitch;
                objectRotation.getEulerZYX(Roll, Yaw, Pitch);

                for (int index = 0; index < 4; index++)
                    LodModels[index].transform = MatrixRotateXYZ((Vector3){ Pitch, Yaw, Roll });
            }
        }
    }


    void setPos(LitVector3 newPos) {
        position = newPos;
        if (boxRigidBody) {
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(newPos.x, newPos.y, newPos.z));
            boxRigidBody->setWorldTransform(transform);
            boxRigidBody->getMotionState()->setWorldTransform(transform);
        }
    }

    void applyForce(const LitVector3& force) {
        if (boxRigidBody && isDynamic) {
            boxRigidBody->setActivationState(ACTIVE_TAG);
            btVector3 btForce(force.x, force.y, force.z);
            boxRigidBody->applyCentralForce(btForce);
        }
    }

    void applyImpulse(const LitVector3& impulse) {
        if (boxRigidBody && isDynamic) {
            boxRigidBody->setActivationState(ACTIVE_TAG);
            btVector3 btImpulse(impulse.x, impulse.y, impulse.z);
            boxRigidBody->applyCentralImpulse(btImpulse);
        }
    }

    void updateMass() {
        if (!isDynamic || dynamicBoxShape == nullptr) return;

        btScalar btMass = mass;
        btVector3 boxInertia = btVector3(inertia.x, inertia.y, inertia.z);
        dynamicBoxShape->calculateLocalInertia(btMass, boxInertia);
        boxRigidBody->setMassProps(btMass, boxInertia);
    }


    void createStaticBox(float x, float y, float z) {
        if (isDynamic) isDynamic = false;
        if (staticBoxShape == nullptr) {
            staticBoxShape = new btBoxShape(btVector3(btScalar(x), btScalar(y), btScalar(z)));

            if (boxRigidBody || dynamicBoxShape || treeRigidBody) {
                dynamicsWorld->removeRigidBody(boxRigidBody);
                delete boxRigidBody->getMotionState();
                delete boxRigidBody;
                delete treeRigidBody;
                boxRigidBody = nullptr;
                dynamicBoxShape = nullptr;
                treeRigidBody = nullptr;
            }

            btTransform groundTransform;
            groundTransform.setIdentity();

            float rollRad = glm::radians(rotation.x);
            float pitchRad = glm::radians(rotation.y);
            float yawRad = glm::radians(rotation.z);

            btQuaternion quaternion;
            quaternion.setEulerZYX(yawRad, pitchRad, rollRad);

            groundTransform.setRotation(quaternion);
            groundTransform.setOrigin(btVector3(position.x, position.y, position.z));

            btDefaultMotionState *groundMotionState = new btDefaultMotionState(groundTransform);
            btRigidBody::btRigidBodyConstructionInfo groundRigidBodyCI(0, groundMotionState, staticBoxShape, btVector3(0, 0, 0));
            boxRigidBody = new btRigidBody(groundRigidBodyCI);

            dynamicsWorld->addRigidBody(boxRigidBody);
            currentCollisionShapeType = None;

            std::cout << "Model - Static" << "\n";
        }
    }

    void createDynamicBox(float x, float y, float z) {
        if (!isDynamic) isDynamic = true;
        if (dynamicBoxShape) {
            dynamicsWorld->removeRigidBody(boxRigidBody);
            delete dynamicBoxShape;
            delete boxMotionState;
            delete boxRigidBody;
            delete treeRigidBody;
            dynamicBoxShape = nullptr;
            boxMotionState = nullptr;
            boxRigidBody = nullptr;
            treeRigidBody = nullptr;
        }
        
        dynamicBoxShape = new btBoxShape(btVector3(btScalar(x), btScalar(y), btScalar(z)));
        currentCollisionShapeType = Box;

        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(position.x, position.y, position.z));

        btScalar btMass(mass);

        btVector3 localInertia(inertia.x, inertia.y, inertia.z);

        dynamicBoxShape->calculateLocalInertia(btMass, localInertia);

        boxMotionState = new btDefaultMotionState(startTransform);
        btRigidBody::btRigidBodyConstructionInfo boxRigidBodyCI(btMass, boxMotionState, dynamicBoxShape, localInertia);
        boxRigidBody = new btRigidBody(boxRigidBodyCI);

        dynamicsWorld->addRigidBody(boxRigidBody);
    }


    void createDynamicMesh() {
        if (!isDynamic) isDynamic = true;

        currentCollisionShapeType = HighPolyMesh;

        if (boxRigidBody || staticBoxShape || treeRigidBody) {
            dynamicsWorld->removeRigidBody(treeRigidBody);
            delete treeRigidBody->getMotionState();
            delete treeRigidBody;
            delete boxRigidBody;
            delete boxMotionState;
            delete staticBoxShape;
            delete dynamicBoxShape;
            treeRigidBody = nullptr;
            boxRigidBody = nullptr;
            boxMotionState = nullptr;
            staticBoxShape = nullptr;
            dynamicBoxShape = nullptr;
            std::cout << "Model - Dynamic" << "\n";
        }

        customMeshShape = new btConvexHullShape();

        // Iterate through the mesh vertices and add them to the convex hull shape
        for (int m = 0; m < model.meshCount; m++) {
            Mesh mesh = model.meshes[m];
            float* meshVertices = (float*)mesh.vertices;

            for (int v = 0; v < mesh.vertexCount; v += 3) {
                customMeshShape->addPoint(btVector3(meshVertices[v], meshVertices[v + 1], meshVertices[v + 2]));
            }
        }

        // Set up the dynamics of your tree object
        btTransform treeTransform;
        treeTransform.setIdentity();
        treeTransform.setOrigin(btVector3(0, 10, 0));

        btScalar treeMass = 1.0f;
        btVector3 treeInertia(0, 0, 0);
        customMeshShape->calculateLocalInertia(treeMass, treeInertia);
        btDefaultMotionState* treeMotionState = new btDefaultMotionState(treeTransform);
        btRigidBody::btRigidBodyConstructionInfo treeRigidBodyCI(treeMass, treeMotionState, customMeshShape, treeInertia);
        treeRigidBody = new btRigidBody(treeRigidBodyCI);

        dynamicsWorld->addRigidBody(treeRigidBody);
    }



    void makePhysicsDynamic(CollisionShapeType shapeType = HighPolyMesh) {
        isDynamic = true;

        if (shapeType == Box)
            createDynamicBox(scale.x, scale.y, scale.z);
        else if (shapeType == HighPolyMesh)
            createDynamicMesh();
    }

    void makePhysicsStatic() {
        isDynamic = false;
        createStaticBox(scale.x, scale.y, scale.z);

    }

    void reloadRigidBody() {
        if (isDynamic)
            makePhysicsDynamic(currentCollisionShapeType);
        else
            makePhysicsStatic();
    }

    void resetPhysics() {
        if (boxRigidBody) {
            boxRigidBody->setLinearVelocity(btVector3(0, 0, 0));
            boxRigidBody->setAngularVelocity(btVector3(0, 0, 0));

            setPos(backupPosition);
        }
    }
}