/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Entity.hpp>
#include <cmath>
#include <raylib.h>

void CreateStressTest();

float getRandomFloat(float min, float max) {
    return min + static_cast<float>(rand()) /
                     static_cast<float>(RAND_MAX / (max - min));
}

void InitStressTest() {
    Entity mainEntity;
    mainEntity.position = {0, 0, 0};
    mainEntity.setName("main");
    mainEntity.setModel("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
    mainEntity.setShader(shaderManager.m_instancingShader);
    entitiesListPregame.emplace_back(mainEntity);

    for (int index = 0; index <= 1000000; index++) {
        CreateStressTest();
    }
}

void CreateStressTest() {
    Vector3 position;
    float random_x = getRandomFloat(-3000.0f, 3000.0f);
    float random_z = getRandomFloat(-3000.0f, 3000.0f);

    position.x = random_x;
    position.y = 0.0f;
    position.z = random_z;

    Entity entity;
    entity.position = position;

    entitiesListPregame.back().addInstance(&entity);
}