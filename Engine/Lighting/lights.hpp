/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef LIGHTS_H
#define LIGHTS_H

#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Textures.hpp>
#include <Engine/GUI/Video/video.hpp>
#include <filesystem>
#include <glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace fs = std::filesystem;

enum LightType { LIGHT_DIRECTIONAL = 0, LIGHT_POINT = 1, LIGHT_SPOT = 2 };

struct SurfaceMaterial;
struct AdditionalLightInfo;
struct LightStruct;

extern std::vector<LightStruct> lights;
extern std::vector<LightStruct> renderModelPreviewerLights;

extern Texture2D lightTexture;

void UpdateLightsBuffer(bool force, std::vector<LightStruct>& lightsVector,
                        GLuint& buffer = lightsBuffer);
LightStruct& NewLight(const Vector3 position, const Color color,
                      int type = LIGHT_POINT, int id = -1);
void removeLight(int id);
int getIdFromLight(const LightStruct& lightStruct);
LightStruct* getLightById(int id);

struct Light {
    int type = LIGHT_POINT;
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 direction = {0.4, 0.4, -0.4};
    alignas(16) glm::vec4 color;
    // params.x = attenuation coeff, params.y = intensity, params.z = SPOT inner cone angle cos
    // params.w = SPOT outer cone angle cos. For point lights, params.w is radius
    alignas(16) glm::vec4 params = {0.001f, 3.0f, 45.0f, 100.0f};
};

struct LightInfo {
    std::string name;
    bool enabled = true;
    alignas(16) glm::vec3 relativePosition;
    alignas(16) glm::vec3 target;
    alignas(4) float innerCutoff = 1.0f;
    alignas(4) float outerCutoff = 3.0f;
};

struct LightStruct {
    Light light;
    LightInfo lightInfo;
    Entity* parent = nullptr;
    int id;
    bool isChild = false;

    LightStruct() {}

    bool operator==(const LightStruct& other) const {
        return (int)this->id == (int)other.id;
    }
};

#endif