#include "../../include_all.h"

void CreateStressTest();

float getRandomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX / (max - min));
}

Entity mainEntity;
Model model;

void InitStressTest() {

    mainEntity.setColor(RED);
    mainEntity.position = {0, 0, 0};
    mainEntity.setScale(Vector3{1, 1, 1});
    mainEntity.setName("main");
    mainEntity.setModel("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
    mainEntity.setShader(instancingShader);
    entitiesListPregame.push_back(mainEntity);

    model = LoadModelFromMesh(GenMeshCube(1, 1, 1));

    for (int index = 0; index <= 500000; index++) {
        CreateStressTest();
    }
}


void CreateStressTest() {
    Vector3 position;
    float random_x = getRandomFloat(-1000.0f, 1000.0f);
    float random_z = getRandomFloat(-1000.0f, 1000.0f);

    position.x = random_x;
    position.y = 0.0f;
    position.z = random_z;

    Entity entity;
    entity.position = position;
    entity.setScale(Vector3{1, 1, 1});
    entity.setModel("", model, instancingShader);
    entity.setShader(instancingShader);
    entity.script = "project/game/stress.py";

    entitiesListPregame.back().addInstance(&entity);
}