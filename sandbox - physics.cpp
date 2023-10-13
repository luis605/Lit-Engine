#include <raylib.h>
#include "btBulletDynamicsCommon.h"
#include <math.h>
#include "raymath.h"        // Required for: MatrixRotateXYZ()

float GetLargestComponent(Vector3 vector) {
    float largest = vector.x;

    if (vector.y > largest) {
        largest = vector.y;
    }

    if (vector.z > largest) {
        largest = vector.z;
    }

    return largest;
}

int main() {
    // Initialize raylib
    InitWindow(800, 600, "Raylib + Bullet Physics Example");

    // Initialize Bullet Physics
    btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);
    btBroadphaseInterface* overlappingPairCache = new btDbvtBroadphase();
    btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver();
    btDiscreteDynamicsWorld* dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -9.81, 0)); // Set gravity

    // Create a raylib sphere mesh
    Model model = LoadModel("assets/models/tree.obj");

    // Create a Bullet Physics shape for the sphere
    // Create a Bullet Physics shape for the sphere
    btTriangleMesh* triMesh = new btTriangleMesh();
    for (int i = 0; i < model.meshCount; i++) {
        Mesh mesh = model.meshes[i];
        int numVerts = mesh.vertexCount;
        int numTriangles = mesh.triangleCount;

        for (int j = 0; j < numTriangles; j++) {
            unsigned int index0 = mesh.indices[j * 3];
            unsigned int index1 = mesh.indices[j * 3 + 1];
            unsigned int index2 = mesh.indices[j * 3 + 2];

            Vector3 v0 = mesh.vertices[index0];
            Vector3 v1 = mesh.vertices[index1];
            Vector3 v2 = mesh.vertices[index2];

            btVector3 vertex0(v0.x, v0.y, v0.z);
            btVector3 vertex1(v1.x, v1.y, v1.z);
            btVector3 vertex2(v2.x, v2.y, v2.z);

            triMesh->addTriangle(vertex0, vertex1, vertex2);
        }
    }

    btBvhTriangleMeshShape* customShape = new btBvhTriangleMeshShape(triMesh, true);




    // Create a Bullet Physics rigid body for the sphere
    btTransform sphereTransform;
    sphereTransform.setIdentity();
    sphereTransform.setOrigin(btVector3(0, 10, 0)); // Position the sphere higher initially

    btDefaultMotionState* sphereMotionState = new btDefaultMotionState(sphereTransform);
    btScalar sphereMass = 1.0f;
    btVector3 sphereInertia(0, 0, 0);
    customShape->calculateLocalInertia(sphereMass, sphereInertia);
    btRigidBody::btRigidBodyConstructionInfo sphereRigidBodyCI(sphereMass, sphereMotionState, customShape, sphereInertia);
    btRigidBody* sphereRigidBody = new btRigidBody(sphereRigidBodyCI);

    // Create a Bullet Physics shape for the inclined floor
    // Calculate the inclination in radians (10 degrees to the right)
    float inclination = 40.0f * DEG2RAD;
    btVector3 planeNormal(cos(inclination), sin(inclination), 0); // Inclined along the X-axis
    btStaticPlaneShape* floorShape = new btStaticPlaneShape(planeNormal, 0.f);
    
    // Create a Bullet Physics rigid body for the inclined floor
    btTransform floorTransform;
    floorTransform.setIdentity();
    floorTransform.setOrigin(btVector3(0, 0, 0)); // Position the floor at the ground level

    btDefaultMotionState* floorMotionState = new btDefaultMotionState(floorTransform);
    btRigidBody::btRigidBodyConstructionInfo floorRigidBodyCI(0.0f, floorMotionState, floorShape, btVector3(0, 0, 0));
    btRigidBody* floorRigidBody = new btRigidBody(floorRigidBodyCI);

    
    // Add the rigid bodies to the dynamics world
    dynamicsWorld->addRigidBody(sphereRigidBody);
    dynamicsWorld->addRigidBody(floorRigidBody);





    // Set up the camera
    Camera3D camera = {0};
    camera.position = (Vector3){0, 30, 30};
    camera.target = (Vector3){0, 3, 0};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 40.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Model plane_model = LoadModelFromMesh(GenMeshPlane(10, 10, 100, 100));


    // Main game loop
    while (!WindowShouldClose()) {
        // Update Bullet Physics
        dynamicsWorld->stepSimulation(1.0f * GetFrameTime(), 10);

        // Get the sphere's transformation
        btTransform sphereTrans;
        if (sphereRigidBody->getMotionState()) {
            sphereRigidBody->getMotionState()->getWorldTransform(sphereTrans);
        }

        // Get the sphere's rotation as Euler angles (in radians)
        btQuaternion sphereRotation = sphereTrans.getRotation();
        btScalar sphereYaw, spherePitch, sphereRoll;
        sphereRotation.getEulerZYX(sphereRoll, sphereYaw, spherePitch);

        // Convert radians to degrees for printing
        float sphereYawDegrees = sphereYaw * RAD2DEG;
        float spherePitchDegrees = spherePitch * RAD2DEG;
        float sphereRollDegrees = sphereRoll * RAD2DEG;

        // Get the sphere's position
        btVector3 spherePosition = sphereTrans.getOrigin();
        Vector3 spherePos = { spherePosition.getX(), spherePosition.getY(), spherePosition.getZ() };

        // Update camera position
        // camera.target = spherePos;

        // Draw the models
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);

        // Apply the sphere's rotation to the cube model
        model.transform = MatrixRotateXYZ((Vector3){ DEG2RAD*spherePitchDegrees, DEG2RAD*sphereYawDegrees, DEG2RAD*sphereRollDegrees });
        DrawModelWires(model, Vector3Zero(), 1.0f , RED);


        // Draw the inclined floor
        DrawGrid(10, 1.0f);

        EndMode3D();
        EndDrawing();
    }

    // Cleanup
    delete sphereRigidBody;
    delete sphereMotionState;
    delete customShape;

    delete floorRigidBody;
    delete floorMotionState;
    delete floorShape;

    delete dynamicsWorld;
    delete solver;
    delete overlappingPairCache;
    delete dispatcher;
    delete collisionConfiguration;

    UnloadModel(model);
    CloseWindow();

    return 0;
}
