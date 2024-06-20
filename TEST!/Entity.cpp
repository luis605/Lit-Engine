#include <iostream>
#include <string>
#include <vector>
#include <type_traits>
#include "globals.h"

// Entity class
class Entity {
public:
    std::string name;
    std::vector<LightType*> lightsChildren;
    std::vector<Entity*>    entitiesChildren;
    int id;

public :
    Entity(std::string newName, int id) {
        name = newName;
        this->id = id;
        std::cout << "Entity created" << std::endl;
    }

    void addLight(LightType* newLight) {
        lightsChildren.emplace_back(newLight);
    }

    void addEntity(Entity* newEntity) {
        entitiesChildren.emplace_back(newEntity);
    }

    template<typename T>
    void addChild(T* obj) {
        if constexpr (std::is_same_v<T, LightType>) {
            std::cout << "Type is Light" << std::endl;
            addLight(obj);
        } else if constexpr (std::is_same_v<T, Entity>) {
            std::cout << "Type is Entity" << std::endl;
            addEntity(obj);
        } else {
            std::cout << "Type is neither Entity nor Light" << std::endl;
        }
    }
};