#include "../../include_all.h"


float getRandomFloat(float min, float max) {
    return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX / (max - min));
}


void InitStressTest() {
    Vector3 position;
    float random_x = getRandomFloat(-100.0f, 100.0f);  // Replace with appropriate range
    float random_z = getRandomFloat(-100.0f, 100.0f);  // Replace with appropriate range

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
    entity.setModel("", model);
    entity.setShader(shader);

    entities_list_pregame.reserve(1);
    entities_list_pregame.emplace_back(entity);

}