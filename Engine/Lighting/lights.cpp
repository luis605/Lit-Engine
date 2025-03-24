#include "lights.hpp"
#include "Shaders.hpp"

std::vector<LightStruct> lights;
std::vector<LightStruct> renderModelPreviewerLights;

Texture2D lightTexture;
Vector4 ambientLight = {1.0f, 1.0f, 1.0f, 1.0f};

LightStruct& NewLight(const Vector3 position, const Color color, int type,
                      int id) {
    glm::vec3 lightsPosition = glm::vec3(position.x, position.y, position.z);
    glm::vec4 lightsColor =
        glm::vec4(color.r / 255, color.g / 255, color.b / 255, color.a / 255);

    LightStruct lightStruct;
    lightStruct.lightInfo.name = "New Light";

    if (id == -1)
        lightStruct.id = lights.size() + entitiesListPregame.size();
    else
        lightStruct.id = id;

    lightStruct.light.type = type;
    lightStruct.light.position = lightsPosition;
    lightStruct.light.color = lightsColor;

    lightIdToIndexMap[lightStruct.id] = lights.size();

    lights.emplace_back(std::move(lightStruct));

    return lights.back();
}

void removeLight(int id) {
    auto it = lightIdToIndexMap.find(id);
    if (it != lightIdToIndexMap.end()) {
        size_t index = it->second;
        lightIdToIndexMap.erase(it);

        if (index != lights.size() - 1) {
            std::swap(lights[index], lights.back());
            lightIdToIndexMap[lights[index].id] = index;
        }
        lights.pop_back();
    }
}

int getIdFromLight(const LightStruct& lightStruct) {
    auto it = std::find(lights.begin(), lights.end(), lightStruct);
    if (it != lights.end()) {
        return it->id;
    }
    return -1;
}

LightStruct* getLightById(int id) {
    auto it = lightIdToIndexMap.find(id);
    if (it != lightIdToIndexMap.end()) {
        return &lights[it->second];
    }
    return nullptr;
}