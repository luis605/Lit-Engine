#ifndef GAME_SHIPPING
    #include "../include_all.h"
#endif

#include "shaders/shaders.h"

#include <string>
#include <iostream>

#ifndef LIGHTS_H
#define LIGHTS_H

typedef struct Light;

typedef enum
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT = 1,
    LIGHT_SPOT = 2
} LightType;


typedef struct SurfaceMaterial;
typedef struct AdditionalLightInfo;

vector<Light> lights;
vector<AdditionalLightInfo> lightsInfo;

void UpdateLightsBuffer(bool force=false, vector<Light> lightsVector = lights);
Light NewLight(const Vector3 position, const Color color, int type = LIGHT_POINT);


Texture2D lightTexture;

int shadowMapWidth = 1024;  // Width of the shadow map texture
int shadowMapHeight = 1024; // Height of the shadow map texture

unsigned int depthMapFBO;

Shader shader;
Shader instancingShader;
Shader downsamplerShader;
Shader upsamplerShader;
GLuint lightsBuffer;

GLuint surfaveMaterialUBO;

Vector4 ambientLight = {1.0f, 1.0f, 1.0f, 1.0f};

bool canAddLight = false;


typedef struct Light
{
    int type = LIGHT_POINT;
    bool enabled = true;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 relativePosition;
    alignas(16) glm::vec3 target;
    alignas(16) glm::vec4 color;
    float attenuation = 0.0001;
    float intensity = 0.5;
    float specularStrength = 0.5;
    float cutOff = 10;
        
    // Others
    bool isChild = false;
    alignas(16) glm::vec3 direction = {0.4, 0.4, -0.4};
    int id;

    bool operator==(const Light& other) const {
        return (int)this->id == (int)other.id;
    }
};


typedef struct SurfaceMaterial
{
    float shininess = 0.5f;
    float SpecularIntensity = 0.5f;
    float Roughness = 0.5f;
    float DiffuseIntensity = 0.5f;
    alignas(16) glm::vec3 SpecularTint = { 1.0f, 1.0f, 1.0f };
    alignas(16) glm::vec3 baseReflectance = { 1.0f, 1.0f, 1.0f };
    alignas(16) glm::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

    fs::path diffuseTexturePath;
    fs::path specularTexturePath;
    fs::path normalTexturePath;
    fs::path roughnessTexturePath;
    fs::path aoTexturePath;
};

typedef struct AdditionalLightInfo
{
    std::string name;
    Entity* parent = nullptr;
    int id;

    bool operator==(const Light& other) const {
        return (int)this->id == (int)other.id;
    }
};


Light NewLight(const Vector3 position, const Color color, int type)
{
    glm::vec3 lightsPosition = glm::vec3(position.x, position.y, position.z);
    glm::vec4 lightsColor = glm::vec4(color.r/255, color.g/255, color.b/255, color.a/255);

    Light light;
    light.type = type;
    light.position = lightsPosition;
    light.color = lightsColor;
    light.id = lights.size() + entitiesListPregame.size() + 1;
    lights.emplace_back(light);


    AdditionalLightInfo info;
    info.name = "Light";
    info.id = light.id;

    lightsInfo.emplace_back(info);

    return lights.back();
}



void UpdateLightsBuffer(bool force, vector<Light> lightsVector)
{
    if (lightsVector.empty() && !force)
        return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);

    // Resize the buffer if the size has changed
    size_t bufferSize = sizeof(Light) * lightsVector.size();
    GLsizeiptr currentBufferSize;
    glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &currentBufferSize);
    if (bufferSize != static_cast<size_t>(currentBufferSize))
    {
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr, GL_DYNAMIC_DRAW);
    }

    // Update the buffer data
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, lightsVector.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer);  // Bind the SSBO to binding point 0

    int lightsCount = lightsVector.size();
    SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);

}


void AddLight()
{
    if (canAddLight)
    {
        Light lightCreate = NewLight({ -2, 1, -2 }, RED);
        lightCreate.isChild = false;
        lightsListPregame.emplace_back(lightCreate);
        canAddLight = false;
    }
}

#endif