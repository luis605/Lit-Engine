module;

#include <cstddef>
#include <cstdint>
#include <functional>

export module Engine.World;

export import :Types;
export import :Pool;
export import :Core;
export import :Builder;

import Engine.Render.entity;
import Engine.glm;
import Engine.Render.reflection;

export bool collectLights(World& world, const glm::vec3& viewPos, LightSet& out, size_t maxLights = 64);
export void registerCoreComponents(World& world);
export struct ReflectionBoxSource {
    std::function<bool(uint32_t, glm::vec3&, glm::vec3&)> meshBox;
    std::function<glm::vec3(uint32_t)> materialColor;
    size_t maxBoxes = kMaxReflectionBoxes;
};
export void collectReflectionVolumes(World& world, ReflectionScene& out);
export void collectReflectionBoxes(World& world, const ReflectionBoxSource& source, ReflectionScene& out);
