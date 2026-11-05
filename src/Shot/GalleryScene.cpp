#include <cstdint>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.Render.component;
import Engine.World;
import Engine.Render.material;
import Engine.Render.settings;

namespace {

constexpr int kSweepCount = 9;
constexpr float kSphereDiameter = 1.6f;
constexpr float kSphereSpacing = 2.0f;
constexpr float kWallHeight = 8.0f;

struct GalleryBuilder {
    Engine& engine;
    World& world;
    uint32_t cube;
    uint32_t sphere;

    explicit GalleryBuilder(Engine& target)
        : engine(target), world(target.world()), cube(target.loadMesh("cube")), sphere(target.loadMesh("sphere")) {}

    static glm::quat aim(const glm::vec3& forward) {
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, forward);
        return glm::quat_cast(glm::mat3(right, up, -forward));
    }

    uint32_t paint(const glm::vec3& color, float roughness, float metallic) {
        Material material;
        material.baseColor = color;
        material.roughness = roughness;
        material.metallic = metallic;
        return engine.createMaterial(material);
    }

    uint32_t glow(const glm::vec3& color, float intensity) {
        Material material;
        material.baseColor = color;
        material.emissive = color;
        material.emissiveIntensity = intensity;
        return engine.createMaterial(material);
    }

    uint32_t glass(const glm::vec3& tint) {
        Material material;
        material.baseColor = tint;
        material.roughness = 0.02f;
        material.transmission = 1.0f;
        material.ior = 1.5f;
        material.thickness = 0.6f;
        return engine.createMaterial(material);
    }

    auto box(const char* name, uint32_t material, const glm::vec3& minCorner, const glm::vec3& size) {
        EntityDesc description;
        description.name = name;
        description.mesh = cube;
        description.material = material;
        description.position = minCorner;
        description.scale = size;
        return world.create(description);
    }

    auto ball(const char* name, uint32_t material, const glm::vec3& baseCenter, float diameter) {
        EntityDesc description;
        description.name = name;
        description.mesh = sphere;
        description.material = material;
        description.position = baseCenter + glm::vec3(0.0f, diameter * 0.5f, 0.0f);
        description.scale = glm::vec3(diameter * 0.5f);
        return world.create(description);
    }

    auto light(const char* name, const LightComponent& component, const glm::vec3& position, const glm::vec3& target) {
        EntityDesc description;
        description.name = name;
        description.position = position;
        if (target != position) description.rotation = aim(glm::normalize(target - position));
        const auto entity = world.create(description);
        world.add<LightComponent>(entity, component);
        return entity;
    }
};

void buildCourtyard(GalleryBuilder& g) {
    const uint32_t floor = g.paint(glm::vec3(0.7f, 0.66f, 0.6f), 0.14f, 0.0f);
    const uint32_t cream = g.paint(glm::vec3(0.86f, 0.82f, 0.74f), 0.85f, 0.0f);
    const uint32_t crimson = g.paint(glm::vec3(0.72f, 0.07f, 0.08f), 0.9f, 0.0f);
    const uint32_t teal = g.paint(glm::vec3(0.05f, 0.5f, 0.55f), 0.9f, 0.0f);
    g.box("courtyard floor", floor, glm::vec3(-12.0f, -0.5f, -12.0f), glm::vec3(24.0f, 0.5f, 24.0f));
    g.box("back wall", cream, glm::vec3(-12.0f, 0.0f, -12.5f), glm::vec3(24.0f, kWallHeight, 0.5f));
    g.box("left wall", crimson, glm::vec3(-12.5f, 0.0f, -12.0f), glm::vec3(0.5f, kWallHeight, 20.0f));
    g.box("right wall", teal, glm::vec3(12.0f, 0.0f, -12.0f), glm::vec3(0.5f, kWallHeight, 20.0f));
}

void buildSweeps(GalleryBuilder& g) {
    const uint32_t plinth = g.paint(glm::vec3(0.3f), 0.5f, 0.0f);
    g.box("sweep plinth", plinth, glm::vec3(-10.0f, 0.0f, 1.8f), glm::vec3(20.0f, 0.12f, 6.4f));
    for (int index = 0; index < kSweepCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kSweepCount - 1);
        const float x = (static_cast<float>(index) - 0.5f * (kSweepCount - 1)) * kSphereSpacing;
        const uint32_t metal = g.paint(glm::vec3(1.0f, 0.78f, 0.34f), t, 1.0f);
        const uint32_t dielectric = g.paint(glm::vec3(0.75f, 0.12f, 0.12f), t, 0.0f);
        g.ball("metal sweep", metal, glm::vec3(x, 0.12f, 3.0f), kSphereDiameter);
        g.ball("dielectric sweep", dielectric, glm::vec3(x, 0.12f, 5.6f), kSphereDiameter);
    }
}

void buildSculpture(GalleryBuilder& g) {
    const uint32_t stone = g.paint(glm::vec3(0.8f, 0.78f, 0.72f), 0.7f, 0.0f);
    const uint32_t bronze = g.paint(glm::vec3(0.72f, 0.45f, 0.2f), 0.25f, 1.0f);
    const uint32_t chrome = g.paint(glm::vec3(0.95f), 0.03f, 1.0f);
    const uint32_t blue = g.paint(glm::vec3(0.1f, 0.25f, 0.85f), 0.35f, 0.0f);
    const uint32_t emberGlow = g.glow(glm::vec3(1.0f, 0.45f, 0.12f), 14.0f);
    const uint32_t iceGlow = g.glow(glm::vec3(0.35f, 0.65f, 1.0f), 12.0f);
    const uint32_t clearGlass = g.glass(glm::vec3(0.9f, 0.97f, 1.0f));
    const uint32_t amberGlass = g.glass(glm::vec3(1.0f, 0.65f, 0.25f));

    g.box("arch left column", stone, glm::vec3(-2.6f, 0.0f, -8.4f), glm::vec3(0.9f, 5.0f, 0.9f));
    g.box("arch right column", stone, glm::vec3(1.7f, 0.0f, -8.4f), glm::vec3(0.9f, 5.0f, 0.9f));
    g.box("arch lintel", stone, glm::vec3(-3.0f, 5.0f, -8.6f), glm::vec3(6.0f, 0.8f, 1.3f));
    g.box("arch keystone", bronze, glm::vec3(-0.5f, 5.8f, -8.5f), glm::vec3(1.0f, 0.7f, 1.1f));
    g.ball("arch chrome orb", chrome, glm::vec3(-0.0f, 0.0f, -8.0f), 2.0f);

    g.box("stack base", stone, glm::vec3(-9.5f, 0.0f, -9.0f), glm::vec3(2.4f, 1.2f, 2.4f));
    g.box("stack middle", blue, glm::vec3(-9.0f, 1.2f, -8.5f), glm::vec3(1.6f, 1.2f, 1.6f));
    g.box("stack top", bronze, glm::vec3(-8.65f, 2.4f, -8.15f), glm::vec3(0.9f, 0.9f, 0.9f));
    g.ball("stack ember", emberGlow, glm::vec3(-8.2f, 3.3f, -7.7f), 0.7f);

    g.box("column low", stone, glm::vec3(5.6f, 0.0f, -9.0f), glm::vec3(0.8f, 2.4f, 0.8f));
    g.box("column mid", stone, glm::vec3(7.0f, 0.0f, -9.0f), glm::vec3(0.8f, 3.6f, 0.8f));
    g.box("column tall", stone, glm::vec3(8.4f, 0.0f, -9.0f), glm::vec3(0.8f, 4.8f, 0.8f));
    g.ball("column ice", iceGlow, glm::vec3(6.0f, 2.4f, -8.6f), 1.1f);

    g.box("glass pedestal left", stone, glm::vec3(-6.4f, 0.0f, -2.5f), glm::vec3(1.4f, 1.1f, 1.4f));
    g.ball("clear glass sphere", clearGlass, glm::vec3(-5.7f, 1.1f, -1.8f), 1.6f);
    g.box("glass pedestal right", stone, glm::vec3(5.0f, 0.0f, -2.5f), glm::vec3(1.4f, 1.1f, 1.4f));
    g.ball("amber glass sphere", amberGlass, glm::vec3(5.7f, 1.1f, -1.8f), 1.6f);
}

void buildLighting(GalleryBuilder& g) {
    LightComponent warmLamp;
    warmLamp.type = LightComponent::Type::Point;
    warmLamp.color = glm::vec3(1.0f, 0.62f, 0.3f);
    warmLamp.intensity = 6000000.0f;
    warmLamp.range = 30.0f;
    warmLamp.sourceRadius = 0.35f;
    g.light("warm lamp", warmLamp, glm::vec3(-8.0f, 4.5f, 0.0f), glm::vec3(-8.0f, 4.5f, 0.0f));

    LightComponent coolTube = warmLamp;
    coolTube.color = glm::vec3(0.55f, 0.72f, 1.0f);
    coolTube.intensity = 9000000.0f;
    coolTube.sourceRadius = 0.15f;
    coolTube.sourceLength = 6.0f;
    g.light("cool tube", coolTube, glm::vec3(0.0f, 6.8f, 1.0f), glm::vec3(0.0f, 6.8f, 1.0f));

    LightComponent spot;
    spot.type = LightComponent::Type::Spot;
    spot.color = glm::vec3(1.0f, 0.85f, 0.65f);
    spot.intensity = 20000000.0f;
    spot.range = 40.0f;
    spot.innerConeDegrees = 14.0f;
    spot.outerConeDegrees = 28.0f;
    spot.sourceRadius = 0.2f;
    g.light("arch spot", spot, glm::vec3(4.0f, 7.5f, 6.0f), glm::vec3(0.0f, 1.5f, -8.0f));

    LightComponent coolSpot = spot;
    coolSpot.color = glm::vec3(0.5f, 0.7f, 1.0f);
    coolSpot.intensity = 14000000.0f;
    g.light("sweep spot", coolSpot, glm::vec3(-7.0f, 7.5f, 8.0f), glm::vec3(-2.0f, 0.0f, 3.5f));

    ReflectionProbeComponent probe;
    probe.halfExtents = glm::vec3(12.0f, 4.0f, 10.0f);
    probe.blendDistance = 3.0f;
    EntityDesc probeDescription;
    probeDescription.name = "courtyard probe";
    probeDescription.position = glm::vec3(0.0f, 3.0f, -1.0f);
    g.world.add<ReflectionProbeComponent>(g.world.create(probeDescription), probe);
}

}

void buildGalleryScene(Engine& engine) {
    GalleryBuilder builder(engine);
    buildCourtyard(builder);
    buildSweeps(builder);
    buildSculpture(builder);
    buildLighting(builder);
    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 17.5f;
    settings.sunAzimuth = 150.0f;
    settings.clouds = true;
    settings.cloudCoverage = 0.5f;
    Camera& camera = builder.world.camera();
    camera.setPos(glm::vec3(0.0f, 5.2f, 17.5f));
    camera.setOrientation(-90.0f, -11.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(400.0f);
}
