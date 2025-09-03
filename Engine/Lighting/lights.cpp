/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Lighting/lights.hpp>
#include <Engine/Lighting/Shaders.hpp>
std::vector<LightStruct> lights;
std::vector<LightStruct> renderModelPreviewerLights;

Texture2D lightTexture;

void UpdateLightsBuffer(bool force, std::vector<LightStruct>& lightsVector, GLuint& buffer) {
    std::vector<Light> lightsData;
    lightsData.reserve(lightsVector.size());

    for (const auto& lightStruct : lightsVector) {
        if (lightStruct.lightInfo.enabled)
            lightsData.emplace_back(lightStruct.light);
    }

    if (lightsData.empty() && !force)
        return;

    size_t bufferSize = sizeof(Light) * lightsData.size();
    GLsizeiptr currentBufferSize;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentBufferSize);

    if (bufferSize != static_cast<size_t>(currentBufferSize)) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    }

    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, lightsData.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);

    int lightsCount = static_cast<int>(lightsData.size());

    // Only update shaders that are valid and expect lights
    for (const std::shared_ptr<Shader>& shader : shaderManager.m_shaders) {
        if (!shader || shader->id == 0)
            continue;

        int location = shaderManager.GetUniformLocation(shader->id, "lightsCount");
        if (location != -1) {
            SetShaderValue(*shader.get(), location, &lightsCount, SHADER_UNIFORM_INT);
        }
    }
}


LightStruct& NewLight(const Vector3 position, const Color color, int type, int id) {
    glm::vec3 lightsPosition = glm::vec3(position.x, position.y, position.z);
    glm::vec4 lightsColor = glm::vec4(color.r / 255, color.g / 255, color.b / 255, color.a / 255);

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