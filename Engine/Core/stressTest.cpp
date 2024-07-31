void CreateStressTest();

float getRandomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX / (max - min));
}

Entity mainEntity;

void InitStressTest() {
    mainEntity.setColor(RED);
    mainEntity.position = {0, 0, 0};
    mainEntity.setName("main");
    mainEntity.setModel("", LoadModelFromMesh(GenMeshCube(1,1,1)));
    mainEntity.setShader(instancingShader);
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