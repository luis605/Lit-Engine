module;

#include <cstdint>
#include <string>
#include "Engine/Tools/TestSceneShared.hpp"

module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.material;

uint32_t testSceneMaterial(Engine& engine, const TestSceneKit& kit, const Material& material);

namespace {

uint32_t paint(Engine& engine, const TestSceneKit& kit, const glm::vec3& color, float roughness, float metallic) {
    Material material;
    material.baseColor = color;
    material.roughness = roughness;
    material.metallic = metallic;
    return testSceneMaterial(engine, kit, material);
}

EntityHandle place(World& world, const char* name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale) {
    EntityDesc description;
    description.name = name;
    description.mesh = mesh;
    description.material = material;
    description.position = position;
    description.scale = scale;
    return world.create(description);
}

}

bool buildShowcaseTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params) {
    World& world = engine.world();
    const uint32_t cube = kit.cube;
    const uint32_t sphere = kit.sphere;
    const uint32_t floor = paint(engine, kit, glm::vec3(0.75f), 0.12f, 0.0f);
    const uint32_t white = paint(engine, kit, glm::vec3(0.8f), 0.9f, 0.0f);
    const uint32_t red = paint(engine, kit, glm::vec3(0.85f, 0.1f, 0.08f), 0.9f, 0.0f);
    const uint32_t green = paint(engine, kit, glm::vec3(0.1f, 0.75f, 0.15f), 0.9f, 0.0f);
    place(world, "floor", cube, floor, glm::vec3(-10.0f, -0.5f, -10.0f), glm::vec3(20.0f, 0.5f, 20.0f));
    place(world, "back wall", cube, white, glm::vec3(-10.0f, 0.0f, -10.5f), glm::vec3(20.0f, 8.0f, 0.5f));
    place(world, "left wall", cube, red, glm::vec3(-10.5f, 0.0f, -10.0f), glm::vec3(0.5f, 8.0f, 20.0f));
    place(world, "right wall", cube, green, glm::vec3(10.0f, 0.0f, -10.0f), glm::vec3(0.5f, 8.0f, 20.0f));
    place(world, "outdoor apron", cube, floor, glm::vec3(-10.0f, -0.5f, 10.0f), glm::vec3(20.0f, 0.5f, 30.0f));
    place(world, "outdoor wall", cube, white, glm::vec3(-10.0f, 0.0f, 24.0f), glm::vec3(20.0f, 4.0f, 0.5f));
    place(world, "ceiling", cube, white, glm::vec3(-10.5f, 8.0f, -10.5f), glm::vec3(21.0f, 0.5f, 21.0f));
    place(world, "pillar", cube, white, glm::vec3(-2.0f, 0.0f, -6.0f), glm::vec3(0.6f, 3.0f, 0.6f));
    place(world, "block", cube, white, glm::vec3(3.0f, 0.0f, -4.0f), glm::vec3(2.0f, 1.0f, 2.0f));
    constexpr int kSphereCount = 5;
    for (int index = 0; index < kSphereCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kSphereCount - 1);
        const uint32_t material = paint(engine, kit, glm::vec3(0.9f, 0.7f, 0.4f), 0.05f + t * 0.6f, index % 2 == 0 ? 1.0f : 0.0f);
        place(world, "sphere", sphere, material, glm::vec3(-6.0f + t * 9.0f, 0.0f, -1.0f), glm::vec3(1.4f));
    }
    LightComponent tube;
    tube.type = LightComponent::Type::Point;
    tube.color = glm::vec3(1.0f, 0.95f, 0.85f);
    tube.intensity = params.lightIntensity;
    tube.range = 30.0f;
    tube.sourceRadius = 0.2f;
    tube.sourceLength = 6.0f;
    world.add<LightComponent>(place(world, "tube light", NO_MESH, 0, glm::vec3(0.0f, 6.5f, -4.0f), glm::vec3(1.0f)), tube);
    LightComponent bulb = tube;
    bulb.sourceLength = 0.0f;
    bulb.sourceRadius = 0.6f;
    bulb.intensity = params.lightIntensity * 0.4f;
    bulb.color = glm::vec3(0.6f, 0.7f, 1.0f);
    world.add<LightComponent>(place(world, "sphere light", NO_MESH, 0, glm::vec3(5.0f, 3.0f, -1.0f), glm::vec3(1.0f)), bulb);
    info.start = glm::vec3(0.0f, 3.0f, 12.0f);
    info.yaw = -90.0f;
    info.pitch = -8.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 200.0f;
    info.suggestedTicks = 60;
    return true;
}

bool buildProbesTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params) {
    World& world = engine.world();
    const uint32_t cube = kit.cube;
    const uint32_t floor = paint(engine, kit, glm::vec3(0.7f), 0.6f, 0.0f);
    const uint32_t red = paint(engine, kit, glm::vec3(0.9f, 0.08f, 0.06f), 0.9f, 0.0f);
    const uint32_t green = paint(engine, kit, glm::vec3(0.08f, 0.8f, 0.12f), 0.9f, 0.0f);
    const uint32_t blue = paint(engine, kit, glm::vec3(0.1f, 0.2f, 0.9f), 0.9f, 0.0f);
    const uint32_t yellow = paint(engine, kit, glm::vec3(0.95f, 0.85f, 0.1f), 0.9f, 0.0f);
    const uint32_t chrome = paint(engine, kit, glm::vec3(0.95f), 0.03f, 1.0f);
    place(world, "floor", cube, floor, glm::vec3(-6.0f, -0.5f, -6.0f), glm::vec3(12.0f, 0.5f, 12.0f));
    place(world, "left wall", cube, red, glm::vec3(-6.5f, 0.0f, -6.0f), glm::vec3(0.5f, 6.0f, 12.0f));
    place(world, "right wall", cube, green, glm::vec3(6.0f, 0.0f, -6.0f), glm::vec3(0.5f, 6.0f, 12.0f));
    place(world, "back wall", cube, blue, glm::vec3(-6.0f, 0.0f, -6.5f), glm::vec3(12.0f, 6.0f, 0.5f));
    place(world, "front wall", cube, yellow, glm::vec3(-6.0f, 0.0f, 6.0f), glm::vec3(12.0f, 6.0f, 0.5f));
    place(world, "ceiling", cube, floor, glm::vec3(-6.5f, 6.0f, -6.5f), glm::vec3(13.0f, 0.5f, 13.0f));
    place(world, "chrome sphere", kit.sphere, chrome, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(2.4f));
    LightComponent lamp;
    lamp.type = LightComponent::Type::Point;
    lamp.color = glm::vec3(1.0f, 0.95f, 0.85f);
    lamp.intensity = params.lightIntensity;
    lamp.range = 30.0f;
    lamp.sourceRadius = 0.3f;
    world.add<LightComponent>(place(world, "lamp", NO_MESH, 0, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f)), lamp);
    if (params.probe) {
        ReflectionProbeComponent probe;
        probe.halfExtents = glm::vec3(6.0f, 3.0f, 6.0f);
        world.add<ReflectionProbeComponent>(place(world, "probe", NO_MESH, 0, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f)), probe);
    }
    info.start = glm::vec3(0.0f, 2.5f, 5.0f);
    info.yaw = -90.0f;
    info.pitch = -10.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 100.0f;
    info.suggestedTicks = 60;
    return true;
}
