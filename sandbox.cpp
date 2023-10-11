#include <raylib.h>
#include "btBulletDynamicsCommon.h"
#include <math.h>

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
    Mesh mesh = GenMeshSphere(1, 50, 50);
    Model model = LoadModelFromMesh(mesh);

    // Create a Bullet Physics shape for the sphere
    btCollisionShape* sphereShape = new btSphereShape(1.0f);

    // Create a Bullet Physics rigid body for the sphere
    btTransform sphereTransform;
    sphereTransform.setIdentity();
    sphereTransform.setOrigin(btVector3(0, 10, 0)); // Position the sphere higher initially

    btDefaultMotionState* sphereMotionState = new btDefaultMotionState(sphereTransform);
    btScalar sphereMass = 1.0f;
    btVector3 sphereInertia(0, 0, 0);
    sphereShape->calculateLocalInertia(sphereMass, sphereInertia);
    btRigidBody::btRigidBodyConstructionInfo sphereRigidBodyCI(sphereMass, sphereMotionState, sphereShape, sphereInertia);
    btRigidBody* sphereRigidBody = new btRigidBody(sphereRigidBodyCI);

    // Create a Bullet Physics shape for the inclined floor
    // Calculate the inclination in radians (10 degrees to the right)
    float inclination = 10.0f * DEG2RAD;
    btVector3 planeNormal(cos(inclination), sin(inclination), 0); // Inclined along the X-axis
    btStaticPlaneShape* floorShape = new btStaticPlaneShape(planeNormal, 0);

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
    camera.position = (Vector3){0, 15, 15};
    camera.target = (Vector3){0, 3, 0};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 40.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Main game loop
    while (!WindowShouldClose()) {
        // Update Bullet Physics
        dynamicsWorld->stepSimulation(1.0f * GetFrameTime(), 10);

        // Get the sphere's transformation
        btTransform trans;
        if (sphereRigidBody->getMotionState()) {
            sphereRigidBody->getMotionState()->getWorldTransform(trans);
        }

        // Get the rotation as Euler angles (in radians)
        btQuaternion rotation = trans.getRotation();
        btScalar yaw, pitch, roll;
        rotation.getEulerZYX(roll, pitch, yaw);

        // Convert radians to degrees for printing
        float rollDegrees = roll * RAD2DEG;
        float pitchDegrees = pitch * RAD2DEG;
        float yawDegrees = yaw * RAD2DEG;

        // Print the rotation angles to the console
        printf("Roll: %f degrees, Pitch: %f degrees, Yaw: %f degrees\n", rollDegrees, pitchDegrees, yawDegrees);
        // Update camera
        if (sphereRigidBody->getMotionState()) {
            sphereRigidBody->getMotionState()->getWorldTransform(trans);
        }
        btVector3 spherePosition = trans.getOrigin();
        Vector3 spherePos = {spherePosition.getX(), spherePosition.getY(), spherePosition.getZ()};

        // Draw the models
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        DrawModelWires(model, spherePos, 1.0f, RED);

        // Draw the inclined floor
        DrawGrid(10, 1.0f);

        EndMode3D();
        EndDrawing();
    }

    // Cleanup
    delete sphereRigidBody;
    delete sphereMotionState;
    delete sphereShape;

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
