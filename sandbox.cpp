#include <raylib.h>
#include "btBulletDynamicsCommon.h"
#include <math.h>
#include "raymath.h"
#include <iostream>

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
    InitWindow(800, 600, "Raylib + Bullet Physics Example");
    
    btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);
    btBroadphaseInterface* overlappingPairCache = new btDbvtBroadphase();
    btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver();
    btDiscreteDynamicsWorld* dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -9.81, 0)); 

    Model treeModel = LoadModel("assets/models/tree.obj");
    
    // Create a convex hull shape for your tree mesh
    btConvexHullShape* treeShape = new btConvexHullShape();

    // Iterate through the mesh vertices and add them to the convex hull shape
    for (int m = 0; m < treeModel.meshCount; m++) {
        Mesh mesh = treeModel.meshes[m];
        float* meshVertices = (float*)mesh.vertices;

        for (int v = 0; v < mesh.vertexCount; v += 3) {
            treeShape->addPoint(btVector3(meshVertices[v], meshVertices[v + 1], meshVertices[v + 2]));
        }
    }

    // Set up the dynamics of your tree object
    btTransform treeTransform;
    treeTransform.setIdentity();
    treeTransform.setOrigin(btVector3(0, 10, 0));

    btScalar treeMass = 1.0f;
    btVector3 treeInertia(0, 0, 0);
    treeShape->calculateLocalInertia(treeMass, treeInertia);
    btDefaultMotionState* treeMotionState = new btDefaultMotionState(treeTransform);
    btRigidBody::btRigidBodyConstructionInfo treeRigidBodyCI(treeMass, treeMotionState, treeShape, treeInertia);
    btRigidBody* treeRigidBody = new btRigidBody(treeRigidBodyCI);

    dynamicsWorld->addRigidBody(treeRigidBody);



    Model plane = LoadModelFromMesh(GenMeshPlane(100, 100, 100, 100));

    float inclination = 40.0f * DEG2RAD;
    btVector3 planeNormal(cos(inclination), sin(inclination), 0); 
    btStaticPlaneShape* floorShape = new btStaticPlaneShape(planeNormal, 0.f);

    
    btTransform floorTransform;
    floorTransform.setIdentity();
    floorTransform.setOrigin(btVector3(0, 0, 0)); 

    btDefaultMotionState* floorMotionState = new btDefaultMotionState(floorTransform);
    btRigidBody::btRigidBodyConstructionInfo floorRigidBodyCI(0.0f, floorMotionState, floorShape, btVector3(0, 0, 0));
    btRigidBody* floorRigidBody = new btRigidBody(floorRigidBodyCI);

    
    dynamicsWorld->addRigidBody(floorRigidBody);

    
    Camera3D camera = {0};
    camera.position = (Vector3){0, 30, 30};
    camera.target = (Vector3){0, 0, 0};
    camera.up = (Vector3){0, 1, 0};
    camera.fovy = 40.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    
    while (!WindowShouldClose()) {
        
        dynamicsWorld->stepSimulation(1.0f * GetFrameTime(), 10);

        
        btTransform treeTrans;
        if (treeRigidBody->getMotionState()) {
            treeRigidBody->getMotionState()->getWorldTransform(treeTrans);
        }

        
        btQuaternion treeRotation = treeTrans.getRotation();
        btScalar treeYaw, treePitch, treeRoll;
        treeRotation.getEulerZYX(treeRoll, treeYaw, treePitch);

        
        float treeYawDegrees = treeYaw * RAD2DEG;
        float treePitchDegrees = treePitch * RAD2DEG;
        float treeRollDegrees = treeRoll * RAD2DEG;

        
        btVector3 treePosition = treeTrans.getOrigin();
        Vector3 treePos = { treePosition.getX(), treePosition.getY(), treePosition.getZ() };

        
        BeginDrawing();
        ClearBackground(GRAY);
        BeginMode3D(camera);

        
        treeModel.transform = MatrixRotateXYZ((Vector3){ DEG2RAD*treePitchDegrees, DEG2RAD*treeYawDegrees, DEG2RAD*treeRollDegrees });
        DrawModelWires(treeModel, Vector3Zero(), 1.0f, RED);


        EndMode3D();

        DrawText(("ROT X: " + std::to_string(treePitchDegrees)).c_str(), 10, 10, 20, BLACK);
        DrawText(("ROT Y: " + std::to_string(treeYawDegrees)).c_str(), 10, 30, 20, BLACK);
        DrawText(("ROT Z: " + std::to_string(treeRollDegrees)).c_str(), 10, 50, 20, BLACK);

        EndDrawing();
    }

    
    delete treeRigidBody;
    delete treeMotionState;
    delete treeShape;

    delete floorRigidBody;
    delete floorMotionState;
    delete floorShape;

    delete dynamicsWorld;
    delete solver;
    delete overlappingPairCache;
    delete dispatcher;
    delete collisionConfiguration;

    UnloadModel(treeModel);
    CloseWindow();

    return 0;
}