#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "globals.h"

struct Light {
    Light() {
        std::cout << "Light created" << std::endl;
    }
};

struct LightInfo {
    std::string name;

    LightInfo(std::string newName) {
        name = newName;
        std::cout << "LightInfo created" << std::endl;
    }

    LightInfo() {
        std::cout << "LightInfo created" << std::endl;
    }
};

struct LightType {
    Light light;
    LightInfo lightInfo;
    int id;

    LightType(std::string newName, int id) {
        lightInfo.name = newName;
        this->id = id;
        std::cout << "LightType created" << std::endl;
    }
};
