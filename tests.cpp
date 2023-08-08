#include "raylib.h"

#include "rlgl.h"
#include "raymath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>

#include "include/bullet3/src/btBulletDynamicsCommon.h"

int main()
{
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "Bullet3 with Raylib Example");

    // Setup camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    
    // Physics world setup
    btBroadphaseInterface* broadphase = new btDbvtBroadphase();
    btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);
    btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver();
    btDiscreteDynamicsWorld* dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -9.81, 0)); // Set gravity

    // Create ground plane
    btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0, 1, 0), 0);
    btDefaultMotionState* groundMotionState = new btDefaultMotionState();
    btRigidBody::btRigidBodyConstructionInfo groundRigidBodyCI(0, groundMotionState, groundShape, btVector3(0, 0, 0));
    btRigidBody* groundRigidBody = new btRigidBody(groundRigidBodyCI);
    dynamicsWorld->addRigidBody(groundRigidBody);

    // Create cube
    btCollisionShape* boxShape = new btBoxShape(btVector3(1, 1, 1));
    btDefaultMotionState* boxMotionState = new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 10, 0)));
    btScalar mass = 1.0f;
    btVector3 boxInertia(0, 0, 0);
    boxShape->calculateLocalInertia(mass, boxInertia);
    btRigidBody::btRigidBodyConstructionInfo boxRigidBodyCI(mass, boxMotionState, boxShape, boxInertia);
    btRigidBody* boxRigidBody = new btRigidBody(boxRigidBodyCI);
    dynamicsWorld->addRigidBody(boxRigidBody);

    SetTargetFPS(60);

    // Main game loop
    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_FREE);
        // Update
        dynamicsWorld->stepSimulation(1.0f / 60.0f);

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // 3D drawing
        BeginMode3D(camera);

        // Draw ground plane
        DrawPlane((Vector3){ 0, 0, 0 }, (Vector2){ 100, 100 }, LIGHTGRAY);

        // Draw cube
        btTransform trans;
        boxRigidBody->getMotionState()->getWorldTransform(trans);
        Vector3 position = { trans.getOrigin().getX(), trans.getOrigin().getY(), trans.getOrigin().getZ() };
        DrawCube(position, 2, 2, 2, RED);

        EndMode3D();

        EndDrawing();
    }

    // Cleanup
    delete dynamicsWorld;
    delete solver;
    delete collisionConfiguration;
    delete dispatcher;
    delete broadphase;

    CloseWindow();

    return 0;
}
