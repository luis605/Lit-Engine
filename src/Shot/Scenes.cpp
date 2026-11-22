#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include "Engine/Log/Log.hpp"
#include "LargeWorldScene.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.Render.component;
import Engine.World;
import Engine.Render.material;
import Engine.TestScenes;
import Engine.GalleryScene;
import Engine.IkTestScene;
import Engine.Render.settings;
import Engine.Render.entity;

LIT_LARGEWORLD_DECLARATIONS

namespace {

uint32_t makeShowcaseMaterial(Engine& engine, const glm::vec3& color, float roughness, float metallic) {
    Material material;
    material.baseColor = color;
    material.roughness = roughness;
    material.metallic = metallic;
    return engine.createMaterial(material);
}

void buildShowcase(Engine& engine, float lightIntensity) {
    World& world = engine.world();
    const uint32_t cube = engine.loadMesh("cube");
    const uint32_t sphere = engine.loadMesh("sphere");
    auto place = [&](const char* name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale) {
        EntityDesc description;
        description.name = name;
        description.mesh = mesh;
        description.material = material;
        description.position = position;
        description.scale = scale;
        return world.create(description);
    };
    const uint32_t floor = makeShowcaseMaterial(engine, glm::vec3(0.75f), 0.12f, 0.0f);
    const uint32_t white = makeShowcaseMaterial(engine, glm::vec3(0.8f), 0.9f, 0.0f);
    const uint32_t red = makeShowcaseMaterial(engine, glm::vec3(0.85f, 0.1f, 0.08f), 0.9f, 0.0f);
    const uint32_t green = makeShowcaseMaterial(engine, glm::vec3(0.1f, 0.75f, 0.15f), 0.9f, 0.0f);
    place("floor", cube, floor, glm::vec3(-10.0f, -0.5f, -10.0f), glm::vec3(20.0f, 0.5f, 20.0f));
    place("back wall", cube, white, glm::vec3(-10.0f, 0.0f, -10.5f), glm::vec3(20.0f, 8.0f, 0.5f));
    place("left wall", cube, red, glm::vec3(-10.5f, 0.0f, -10.0f), glm::vec3(0.5f, 8.0f, 20.0f));
    place("right wall", cube, green, glm::vec3(10.0f, 0.0f, -10.0f), glm::vec3(0.5f, 8.0f, 20.0f));
    place("outdoor apron", cube, floor, glm::vec3(-10.0f, -0.5f, 10.0f), glm::vec3(20.0f, 0.5f, 30.0f));
    place("outdoor wall", cube, white, glm::vec3(-10.0f, 0.0f, 24.0f), glm::vec3(20.0f, 4.0f, 0.5f));
    place("ceiling", cube, white, glm::vec3(-10.5f, 8.0f, -10.5f), glm::vec3(21.0f, 0.5f, 21.0f));
    place("pillar", cube, white, glm::vec3(-2.0f, 0.0f, -6.0f), glm::vec3(0.6f, 3.0f, 0.6f));
    place("block", cube, white, glm::vec3(3.0f, 0.0f, -4.0f), glm::vec3(2.0f, 1.0f, 2.0f));
    constexpr int kSphereCount = 5;
    for (int index = 0; index < kSphereCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kSphereCount - 1);
        const uint32_t material = makeShowcaseMaterial(engine, glm::vec3(0.9f, 0.7f, 0.4f), 0.05f + t * 0.6f, index % 2 == 0 ? 1.0f : 0.0f);
        place("sphere", sphere, material, glm::vec3(-6.0f + t * 9.0f, 0.0f, -1.0f), glm::vec3(1.4f));
    }
    LightComponent tube;
    tube.type = LightComponent::Type::Point;
    tube.color = glm::vec3(1.0f, 0.95f, 0.85f);
    tube.intensity = lightIntensity;
    tube.range = 30.0f;
    tube.sourceRadius = 0.2f;
    tube.sourceLength = 6.0f;
    world.add<LightComponent>(place("tube light", NO_MESH, 0, glm::vec3(0.0f, 6.5f, -4.0f), glm::vec3(1.0f)), tube);
    LightComponent bulb = tube;
    bulb.sourceLength = 0.0f;
    bulb.sourceRadius = 0.6f;
    bulb.intensity = lightIntensity * 0.4f;
    bulb.color = glm::vec3(0.6f, 0.7f, 1.0f);
    world.add<LightComponent>(place("sphere light", NO_MESH, 0, glm::vec3(5.0f, 3.0f, -1.0f), glm::vec3(1.0f)), bulb);
    Camera& camera = world.camera();
    camera.setPos(glm::vec3(0.0f, 3.0f, 12.0f));
    camera.setOrientation(-90.0f, -8.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(200.0f);
}

void buildProbes(Engine& engine, float lightIntensity, bool withProbe) {
    World& world = engine.world();
    const uint32_t cube = engine.loadMesh("cube");
    const uint32_t sphere = engine.loadMesh("sphere");
    auto place = [&](const char* name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale) {
        EntityDesc description;
        description.name = name;
        description.mesh = mesh;
        description.material = material;
        description.position = position;
        description.scale = scale;
        return world.create(description);
    };
    const uint32_t floor = makeShowcaseMaterial(engine, glm::vec3(0.7f), 0.6f, 0.0f);
    const uint32_t red = makeShowcaseMaterial(engine, glm::vec3(0.9f, 0.08f, 0.06f), 0.9f, 0.0f);
    const uint32_t green = makeShowcaseMaterial(engine, glm::vec3(0.08f, 0.8f, 0.12f), 0.9f, 0.0f);
    const uint32_t blue = makeShowcaseMaterial(engine, glm::vec3(0.1f, 0.2f, 0.9f), 0.9f, 0.0f);
    const uint32_t yellow = makeShowcaseMaterial(engine, glm::vec3(0.95f, 0.85f, 0.1f), 0.9f, 0.0f);
    const uint32_t chrome = makeShowcaseMaterial(engine, glm::vec3(0.95f), 0.03f, 1.0f);
    place("floor", cube, floor, glm::vec3(-6.0f, -0.5f, -6.0f), glm::vec3(12.0f, 0.5f, 12.0f));
    place("left wall", cube, red, glm::vec3(-6.5f, 0.0f, -6.0f), glm::vec3(0.5f, 6.0f, 12.0f));
    place("right wall", cube, green, glm::vec3(6.0f, 0.0f, -6.0f), glm::vec3(0.5f, 6.0f, 12.0f));
    place("back wall", cube, blue, glm::vec3(-6.0f, 0.0f, -6.5f), glm::vec3(12.0f, 6.0f, 0.5f));
    place("front wall", cube, yellow, glm::vec3(-6.0f, 0.0f, 6.0f), glm::vec3(12.0f, 6.0f, 0.5f));
    place("ceiling", cube, floor, glm::vec3(-6.5f, 6.0f, -6.5f), glm::vec3(13.0f, 0.5f, 13.0f));
    place("chrome sphere", sphere, chrome, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.4f));
    LightComponent lamp;
    lamp.type = LightComponent::Type::Point;
    lamp.color = glm::vec3(1.0f, 0.95f, 0.85f);
    lamp.intensity = lightIntensity;
    lamp.range = 30.0f;
    lamp.sourceRadius = 0.3f;
    world.add<LightComponent>(place("lamp", NO_MESH, 0, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f)), lamp);
    if (withProbe) {
        ReflectionProbeComponent probe;
        probe.halfExtents = glm::vec3(6.0f, 3.0f, 6.0f);
        world.add<ReflectionProbeComponent>(place("probe", NO_MESH, 0, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f)), probe);
    }
    Camera& camera = world.camera();
    camera.setPos(glm::vec3(0.0f, 2.5f, 5.0f));
    camera.setOrientation(-90.0f, -10.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(100.0f);
}

// light_stress: an open field of pillars and bobbing spheres lit at night by thousands of moving
// point and spot lights (every one shadowed when virtual shadow maps are on). Animation is driven by
// the frame index, not wall time, so screenshots are repeatable.
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

StressState gStress;

float stressRandom(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f;
}

void buildLightStress(Engine& engine, int lightCount) {
    World& world = engine.world();
    gStress = {};
    const uint32_t cube = engine.loadMesh("cube");
    const uint32_t sphere = engine.loadMesh("sphere");
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
    const uint32_t ground = makeShowcaseMaterial(engine, glm::vec3(0.55f), 0.7f, 0.0f);
    const uint32_t stone = makeShowcaseMaterial(engine, glm::vec3(0.7f, 0.68f, 0.62f), 0.8f, 0.0f);
    const uint32_t metal = makeShowcaseMaterial(engine, glm::vec3(0.9f, 0.75f, 0.5f), 0.3f, 1.0f);
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
        gStress.casters.push_back({place("bobber", sphere, metal, base, glm::vec3(0.8f)), base, stressRandom(seed) * 6.2831853f});
    }
    const glm::quat down = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
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
        gStress.lights.push_back({entity, center, 0.5f + 2.5f * stressRandom(seed), 0.3f + 0.9f * stressRandom(seed), stressRandom(seed) * 6.2831853f});
    }
    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 23.0f;
    Camera& camera = world.camera();
    camera.setPos(glm::vec3(0.0f, 16.0f, 62.0f));
    camera.setOrientation(-90.0f, -22.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(400.0f);
}

}

void animateVsmMixed(Engine& engine, int frame);

void animateScene(Engine& engine, int frame) {
    animateVsmMixed(engine, frame);
    if (gStress.lights.empty() && gStress.casters.empty()) return;
    const float time = static_cast<float>(frame) / 60.0f;
    std::vector<std::pair<EntityHandle, glm::vec3>> updates;
    updates.reserve(gStress.lights.size() + gStress.casters.size());
    for (const StressLight& light : gStress.lights) {
        const float angle = light.phase + time * light.speed;
        updates.emplace_back(light.entity, light.center + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * light.radius);
    }
    for (const StressCaster& caster : gStress.casters) updates.emplace_back(caster.entity, caster.base + glm::vec3(0.0f, 1.2f * (0.5f + 0.5f * std::sin(caster.phase + time * 1.7f)), 0.0f));
    engine.world().setPositions(updates);
}

namespace {

}

void buildGallery(Engine& engine) {
    GalleryKit kit;
    kit.cube = engine.loadMesh("cube");
    kit.sphere = engine.loadMesh("sphere");
    kit.material = [&engine](const Material& material) { return engine.createMaterial(material); };
    buildGalleryScene(engine, kit);
}

void buildIkTest(Engine& engine) {
    IkTestKit kit;
    kit.cube = engine.loadMesh("cube");
    kit.sphere = engine.loadMesh("sphere");
    kit.debugDraw = true;
    kit.material = [&engine](const Material& material) { return engine.createMaterial(material); };
    buildIkTestScene(engine, kit);
}

void buildDense(Engine& engine, int grid);
void buildVsmMixed(Engine& engine, int lightCount);

void buildScene(Engine& engine, const ShotOptions& options) {
    if (options.testScene == "dense") {
        buildDense(engine, options.denseGrid);
        return;
    }
    if (!options.sceneFile.empty()) {
        engine.preloadSceneMeshes(options.sceneFile);
        if (!engine.world().loadScene(options.sceneFile)) Lit::Log::Error("Could not load scene {}", options.sceneFile);
        return;
    }
    if (options.testScene == "showcase") {
        buildShowcase(engine, options.lightIntensity);
        return;
    }
    if (options.testScene == "probes") {
        buildProbes(engine, options.lightIntensity, !options.noProbe);
        return;
    }
    if (options.testScene == "light_stress") {
        buildLightStress(engine, options.lightCount);
        return;
    }
    if (options.testScene == "largeworld") {
        LargeWorldConfig config;
        config.originKm = options.originKm;
        config.flySpeed = options.flySpeed;
        config.rebaseFrame = options.rebaseFrame;
        buildLargeWorldScene(engine, config);
        return;
    }
    if (options.testScene == "vsm_mixed") {
        buildVsmMixed(engine, options.lightCount);
        return;
    }
    if (options.testScene == "ik") {
        buildIkTest(engine);
        return;
    }
    if (options.testScene == "gallery") {
        buildGallery(engine);
        return;
    }
    TestSceneKit kit;
    kit.cube = engine.loadMesh("cube");
    kit.sphere = engine.loadMesh("sphere");
    kit.material = [&engine](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
        Material material;
        material.baseColor = color;
        material.roughness = roughness;
        material.emissive = emissive;
        material.emissiveIntensity = intensity;
        return engine.createMaterial(material);
    };
    TestSceneInfo info;
    if (buildTestScene(engine, options.testScene, kit, info)) {
        applyTestSceneCamera(engine, info);
    } else {
        Lit::Log::Error("Unknown test scene {}", options.testScene);
    }
}
