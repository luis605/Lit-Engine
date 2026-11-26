module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Engine/Tools/TestSceneShared.hpp"

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.material;
import Engine.Render.settings;

uint32_t testSceneMaterial(Engine& engine, const TestSceneKit& kit, const Material& material);

namespace {

struct StressLight {
    EntityHandle entity;
    glm::vec3 center;
    float radius;
    float speed;
    float phase;
};

struct StressCaster {
    EntityHandle entity;
    glm::vec3 base;
    float phase;
};

struct StressState {
    std::vector<StressLight> lights;
    std::vector<StressCaster> casters;
};

float stressRandom(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f;
}

uint32_t paint(Engine& engine, const TestSceneKit& kit, const glm::vec3& color, float roughness, float metallic) {
    Material material;
    material.baseColor = color;
    material.roughness = roughness;
    material.metallic = metallic;
    return testSceneMaterial(engine, kit, material);
}

}

bool buildLightStressTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params) {
    World& world = engine.world();
    auto state = std::make_shared<StressState>();
    const uint32_t cube = kit.cube;
    const uint32_t sphere = kit.sphere;
    auto place = [&](const char* name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
        EntityDesc description;
        description.name = name;
        description.mesh = mesh;
        description.material = material;
        description.position = position;
        description.scale = scale;
        description.rotation = rotation;
        return world.create(description);
    };
    constexpr float kHalf = 80.0f;
    const uint32_t ground = paint(engine, kit, glm::vec3(0.55f), 0.7f, 0.0f);
    const uint32_t stone = paint(engine, kit, glm::vec3(0.7f, 0.68f, 0.62f), 0.8f, 0.0f);
    const uint32_t metal = paint(engine, kit, glm::vec3(0.9f, 0.75f, 0.5f), 0.3f, 1.0f);
    place("ground", cube, ground, glm::vec3(-kHalf, -0.5f, -kHalf), glm::vec3(2.0f * kHalf, 0.5f, 2.0f * kHalf));
    uint32_t seed = 12345u;
    constexpr int kGrid = 30;
    constexpr float kSpacing = 2.0f * kHalf / kGrid;
    for (int z = 0; z < kGrid; ++z) {
        for (int x = 0; x < kGrid; ++x) {
            const float height = 0.5f + 3.5f * stressRandom(seed);
            const float width = 0.6f + 0.8f * stressRandom(seed);
            const glm::vec3 corner(-kHalf + (static_cast<float>(x) + 0.5f) * kSpacing - width * 0.5f, 0.0f, -kHalf + (static_cast<float>(z) + 0.5f) * kSpacing - width * 0.5f);
            place("pillar", cube, stone, corner, glm::vec3(width, height, width));
        }
    }
    constexpr int kMovingCasters = 256;
    for (int index = 0; index < kMovingCasters; ++index) {
        const glm::vec3 base(-kHalf + 2.0f * kHalf * stressRandom(seed), 0.8f, -kHalf + 2.0f * kHalf * stressRandom(seed));
        state->casters.push_back({place("bobber", sphere, metal, base, glm::vec3(0.8f)), base, stressRandom(seed) * 6.2831853f});
    }
    const glm::quat down = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    const int lightCount = std::max(0, params.lightCount);
    for (int index = 0; index < lightCount; ++index) {
        const bool spot = index % 4 == 3;
        LightComponent light;
        light.type = spot ? LightComponent::Type::Spot : LightComponent::Type::Point;
        const float hue = stressRandom(seed) * 6.0f;
        light.color = glm::clamp(glm::vec3(std::abs(hue - 3.0f) - 1.0f, 2.0f - std::abs(hue - 2.0f), 2.0f - std::abs(hue - 4.0f)), 0.0f, 1.0f) * 0.7f + 0.3f;
        light.intensity = spot ? 1.2e5f : 6.0e4f;
        light.range = 5.0f + 4.0f * stressRandom(seed);
        light.innerConeDegrees = 25.0f;
        light.outerConeDegrees = 40.0f;
        const glm::vec3 center(-kHalf + 2.0f * kHalf * stressRandom(seed), 1.2f + 2.3f * stressRandom(seed), -kHalf + 2.0f * kHalf * stressRandom(seed));
        const EntityHandle entity = place(spot ? "stress spot" : "stress light", NO_MESH, 0, center, glm::vec3(1.0f), spot ? down : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        world.add<LightComponent>(entity, light);
        state->lights.push_back({entity, center, 0.5f + 2.5f * stressRandom(seed), 0.3f + 0.9f * stressRandom(seed), stressRandom(seed) * 6.2831853f});
    }
    engine.renderSettings().timeOfDay = 23.0f;
    engine.addSystem(Phase::Update, "testscene.light_stress", [state](World& scene, float) {
        const float time = static_cast<float>(scene.time().elapsed);
        std::vector<std::pair<EntityHandle, glm::vec3>> updates;
        updates.reserve(state->lights.size() + state->casters.size());
        for (const StressLight& light : state->lights) {
            const float angle = light.phase + time * light.speed;
            updates.emplace_back(light.entity, light.center + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * light.radius);
        }
        for (const StressCaster& caster : state->casters) updates.emplace_back(caster.entity, caster.base + glm::vec3(0.0f, 1.2f * (0.5f + 0.5f * std::sin(caster.phase + time * 1.7f)), 0.0f));
        scene.setPositions(updates);
    });
    info.start = glm::vec3(0.0f, 16.0f, 62.0f);
    info.yaw = -90.0f;
    info.pitch = -22.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 400.0f;
    info.suggestedTicks = 120;
    Engine* enginePointer = &engine;
    info.metrics = [enginePointer, state]() {
        TestSceneMetrics metrics;
        metrics.set("entities", static_cast<double>(enginePointer->world().aliveCount()));
        metrics.set("moving_lights", static_cast<double>(state->lights.size()));
        metrics.set("moving_casters", static_cast<double>(state->casters.size()));
        return metrics;
    };
    return true;
}
