#include <cstdint>
#include "Engine/Log/Log.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.Render.component;
import Engine.World;
import Engine.Render.material;
import Engine.TestScenes;
import Engine.GalleryScene;

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

}

void buildGallery(Engine& engine) {
    GalleryKit kit;
    kit.cube = engine.loadMesh("cube");
    kit.sphere = engine.loadMesh("sphere");
    kit.material = [&engine](const Material& material) { return engine.createMaterial(material); };
    buildGalleryScene(engine, kit);
}

void buildScene(Engine& engine, const ShotOptions& options) {
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
