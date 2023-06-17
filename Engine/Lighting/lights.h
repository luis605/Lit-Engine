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

typedef struct
{
    int type;
    bool enabled;
    Vector3 position;
    Vector3 relative_position;
    Vector3 target;
    Color color;
    float attenuation;

    // Shader locations
    int enabledLoc;
    int typeLoc;
    int positionLoc;
    int targetLoc;
    int colorLoc;
    int attenuationLoc;

    bool isChild;

    std::string name;
    std::string id = "";
} Light;

vector<Light> lights;

Light NewLight(const Vector3 position, const Color color)
{
    Light light;
    light.position = position;
    light.color = color;
    std::cout << "Color: " << static_cast<int>(light.color.r) << ", " << static_cast<int>(light.color.g) << ", " << static_cast<int>(light.color.b) << ", " << static_cast<int>(light.color.a) << std::endl;
    lights.push_back(light);
    std::cout << "light added" << std::endl;
    return lights.back();
}


float3 vectorToFloat3(Vector3 vector)
{
    return {vector.x, vector.y, vector.z};
}

void UpdateLightsBuffer()
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * lights.size(), lights.data(), GL_DYNAMIC_DRAW);
    
    std::cout << lights[0].color.a;
    int lightsCount = lights.size();
    SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);
}

#endif