#include "../../include_all.h"


float getRandomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX / (max - min));
}

void CreateStressTest();


Entity main_entity;


void InitStressTest() {

    main_entity.setColor(RED);
    main_entity.position = {0, 0, 0};
    main_entity.setScale(Vector3{1, 1, 1});
    main_entity.setName("main");
    main_entity.setModel("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
    main_entity.setShader(instancing_shader);
    entities_list_pregame.push_back(main_entity);

    for (int index = 0; index <= 500000; index++)
    {
        CreateStressTest();
    }
}


void CreateStressTest() {
    Vector3 position;
    float random_x = getRandomFloat(-1000.0f, 1000.0f);  // Replace with appropriate range
    float random_z = getRandomFloat(-1000.0f, 1000.0f);  // Replace with appropriate range

    position.x = random_x;
    position.y = 0.0f;  // Assuming you want the y-coordinate to be 0
    position.z = random_z;

    Model model = { 0 };
    model = LoadModelFromMesh(GenMeshCube(1, 1, 1));

    Entity entity;
    entity.setColor(RED);
    entity.position = position;
    entity.setScale(Vector3{1, 1, 1});
    entity.setName("stress test");
    entity.setModel("", model, instancing_shader);
    entity.setShader(instancing_shader);
    entity.script = "project/game/stress.py";

    entities_list_pregame.back().addInstance(&entity);
}