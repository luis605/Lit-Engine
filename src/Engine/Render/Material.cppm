module;

#include <cstdint>
#include <string>

export module Engine.Render.material;

import Engine.glm;

export inline constexpr int NO_TEXTURE = -1;

export struct Material {
    std::string name = "Material";
    glm::vec3 baseColor{0.8f, 0.8f, 0.8f};
    float opacity = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float reflectance = 0.5f;
    float occlusion = 1.0f;
    glm::vec3 emissive{1.0f, 1.0f, 1.0f};
    float emissiveIntensity = 0.0f;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.05f;
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;
    glm::vec3 sheenColor{0.0f, 0.0f, 0.0f};
    float sheenRoughness = 0.4f;
    glm::vec3 subsurfaceColor{1.0f, 0.35f, 0.25f};
    float subsurface = 0.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float thickness = 0.5f;
    float normalStrength = 1.0f;
    float uvScale = 1.0f;
    bool triplanar = false;
    int baseColorTexture = NO_TEXTURE;
    int normalTexture = NO_TEXTURE;
    int ormTexture = NO_TEXTURE;
    int emissiveTexture = NO_TEXTURE;
    glm::vec4 custom0{0.0f};
    glm::vec4 custom1{0.0f};
};
