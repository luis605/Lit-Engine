#include <iostream>
#include <vector>
#include "globals.h"
#include "Entity.cpp"
#include "Light.cpp"

std::vector<Entity*> entitiesList;
std::vector<LightType*> lightsList;

int main() {
    // Create entities
    Entity entity1("Entity 1", 1);
    entitiesList.emplace_back(&entity1);

    LightType lightType("Light 1", 1);
    lightsList.emplace_back(&lightType);

    std::cout << "\n\n\n=================\n\n\n";

    entity1.addChild(&lightType);

    return 0;
}
