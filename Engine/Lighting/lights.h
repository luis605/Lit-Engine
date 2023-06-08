#include "../../include_all.h"
#include <string>
#include <iostream>

#ifndef LIGHTS_H
#define LIGHTS_H

typedef enum
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT
} LightType;

typedef struct Light
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

    bool isChildren;

    string name;
};

vector<Light> lights;
Light* lightsArray;
int numLights;

void InitializeUBO()
{
    lightsArray = lights.data();
    numLights = lights.size();
}

void UpdateUBO()
{
    // Update the UBO data with the latest lights information
//    UniformBufferUpdate(lightsUBO, sizeof(Light) * lights.size(), lights.data());
}

Light NewLight(const Vector3 position, const Color color)
{
    Light light;
    light.position = position;
    light.color = color;
    lights.push_back(light);
    std::cout << "light added" << std::endl;
    return lights.back();
}

#endif