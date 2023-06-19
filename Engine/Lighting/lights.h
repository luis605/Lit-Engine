#include "../../include_all.h"
#include <string>
#include <iostream>

#ifndef LIGHTS_H
#define LIGHTS_H

Shader shader;

GLuint lightsBuffer;

typedef enum
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT
} LightType;

typedef struct Light
{
    int type;
    bool enabled;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 relative_position;
    alignas(16) glm::vec3 target;
    alignas(16) glm::vec4 color;
    float attenuation;
    bool isChild;
};

typedef struct AdditionalLightInfo
{
    string name;
    string id;
};

vector<Light> lights;
vector<AdditionalLightInfo> lights_info;

Light NewLight(const Vector3 position, const Color color)
{
    glm::vec3 lights_position = glm::vec3(position.x, position.y, position.z);
    glm::vec4 lights_color = glm::vec4(color.r/255, color.g/255, color.b/255, color.a/255);

    Light light;
    light.position = lights_position;
    light.color = lights_color;
    lights.push_back(light);


    AdditionalLightInfo info;
    info.name = "Light";

    lights_info.push_back(info);

    return lights.back();
}



void UpdateLightsBuffer()
{
    if (lights.empty())
        return;

    std::cout << "Colors: " << lights[0].color.r << " " << lights[0].color.g << " " << lights[0].color.b << " " << lights[0].color.a << std::endl;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);

    // Resize the buffer if the size has changed
    size_t bufferSize = sizeof(Light) * lights.size();
    GLsizeiptr currentBufferSize;
    glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentBufferSize);
    if (bufferSize != static_cast<size_t>(currentBufferSize))
    {
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    }

    // Update the buffer data
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, lights.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer);  // Bind the SSBO to binding point 0

    int lightsCount = lights.size();
    std::cout << lightsCount << std::endl;
    SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
}




#endif