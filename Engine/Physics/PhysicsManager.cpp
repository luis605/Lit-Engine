/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Physics/PhysicsManager.hpp>

float scaleFactorRaylibBullet = 0.5f;

void PhysicsManager::Init() {
    broadphase = new btDbvtBroadphase();
    collisionConfiguration = new btDefaultCollisionConfiguration();
    dispatcher = new btCollisionDispatcher(collisionConfiguration);
    solver = new btSequentialImpulseConstraintSolver();
    dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver,
                                                collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -9.81, 0));
    gravity.x = dynamicsWorld->getGravity().x();
    gravity.y = dynamicsWorld->getGravity().y();
    gravity.z = dynamicsWorld->getGravity().z();
}

void PhysicsManager::Update(float deltaTime) {
    if (dynamicsWorld)
        dynamicsWorld->stepSimulation(deltaTime, 10);
    else {
        dynamicsWorld = new btDiscreteDynamicsWorld(
            dispatcher, broadphase, solver, collisionConfiguration);
        dynamicsWorld->setGravity(btVector3(0, -9.81, 0));
    }
}

void PhysicsManager::SetGravity(const LitVector3& gravity) {
    dynamicsWorld->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
    this->gravity.x = dynamicsWorld->getGravity().x();
    this->gravity.y = dynamicsWorld->getGravity().y();
    this->gravity.z = dynamicsWorld->getGravity().z();
}

void PhysicsManager::Backup() { this->backupGravity = this->gravity; }
void PhysicsManager::UnBackup() { this->SetGravity(this->backupGravity); }

PhysicsManager physics;
