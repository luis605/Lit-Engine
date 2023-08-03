#ifndef GAME_SHIPPING
    #include "../include_all.h"
#endif

#include <string>
#include <iostream>

#ifndef LIGHTS_H
#define LIGHTS_H



Texture2D light_texture;

int shadowMapWidth = 1024;  // Width of the shadow map texture
int shadowMapHeight = 1024; // Height of the shadow map texture

unsigned int depthMapFBO;

Shader shader;
GLuint lightsBuffer;

GLuint surface_material_ubo;

bool canAddLight = false;



struct pbrFlags {
    bool normalMapInit;
    bool roughnessMapInit;
    bool diffuseMapInit;
};

pbrFlags pbr_flags;


typedef enum
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT = 1,
    LIGHT_SPOT = 2
} LightType;

typedef struct Light
{
    int type = LIGHT_POINT;
    bool enabled = true;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 relative_position;
    alignas(16) glm::vec3 target;
    alignas(16) glm::vec4 color;
    float attenuation = 0.0001;
    float intensity = 0.5;
    float specularStrength = 0.5;
    float cutOff = 10;
    bool isChild;
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
};

typedef struct AdditionalLightInfo
{
    string name;
    int id;

    bool operator==(const Light& other) const {
        return (int)this->id == (int)other.id;
    }
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
    light.id = lights.size() + entities_list_pregame.size() + 1;
    lights.push_back(light);


    AdditionalLightInfo info;
    info.name = "Light";
    info.id = light.id;

    lights_info.push_back(info);

    return lights.back();
}



void UpdateLightsBuffer(bool force=false, vector<Light> lights = lights)
{
    if (lights.empty() && !force)
        return;

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
    SetShaderValue(shader, GetShaderLocation(shader, "lightsCount"), &lightsCount, SHADER_UNIFORM_INT);

}


void AddLight()
{
    if (canAddLight)
    {
        cout << "AddLight" << endl;
        Light light_create = NewLight((Vector3){ -2, 1, -2 }, RED);
        lights_list_pregame.push_back(light_create);
        canAddLight = false;
    }
}

#endif