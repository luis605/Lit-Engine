#include "LargeWorldScene.hpp"

#include <cmath>
#include <cstdint>
#include <typeinfo>

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.Render.component;
import Engine.World;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.entity;
import Engine.Render.reflection;

LIT_LARGEWORLD_DECLARATIONS

namespace {

struct State {
    LargeWorldConfig config;
    glm::dvec3 start{0.0};
    glm::dvec3 camera{0.0};  // driven in world coordinates
    bool flying = false;
};

State gState;

constexpr double kCorridorLength = 4200.0;
constexpr int kCheckerShader = 1;

uint32_t makeMaterial(Engine& engine, const glm::vec3& color, float roughness, float metallic = 0.0f) {
    Material material;
    material.baseColor = color;
    material.roughness = roughness;
    material.metallic = metallic;
    return engine.createMaterial(material);
}

}

void buildLargeWorldScene(Engine& engine, const LargeWorldConfig& config) {
    gState = State{};
    gState.config = config;
    World& world = engine.world();
    const double originX = config.originKm * 1000.0;
    const double originZ = -0.5 * originX;
    // Put the origin at the scene before anything exists: floats stay small, world coordinates stay huge.
    engine.rebaseOrigin(glm::dvec3(originX, 0.0, originZ));
    if (config.autoRebase) {
        FloatingOriginConfig origin;
        origin.threshold = config.rebaseThreshold;
        origin.snap = config.rebaseSnap;
        engine.enableFloatingOrigin(origin);
    }

    const uint32_t cube = engine.loadMesh("cube");
    const uint32_t sphere = engine.loadMesh("sphere");
    auto place = [&](const char* name, uint32_t mesh, uint32_t material, const glm::dvec3& corner, const glm::vec3& size, uint32_t shader = 0) {
        EntityDesc description;
        description.name = name;
        description.mesh = mesh;
        description.material = material;
        description.shader = shader;
        description.position = world.toLocal(corner);
        description.scale = size;
        return world.create(description);
    };
    const glm::dvec3 base(originX, 0.0, originZ);
    const uint32_t ground = makeMaterial(engine, glm::vec3(0.62f, 0.6f, 0.55f), 0.8f);
    const uint32_t stone = makeMaterial(engine, glm::vec3(0.75f, 0.72f, 0.66f), 0.7f);
    const uint32_t red = makeMaterial(engine, glm::vec3(0.9f, 0.1f, 0.08f), 0.6f);
    const uint32_t blue = makeMaterial(engine, glm::vec3(0.1f, 0.2f, 0.85f), 0.6f);
    const uint32_t green = makeMaterial(engine, glm::vec3(0.15f, 0.7f, 0.2f), 0.6f);
    const uint32_t metal = makeMaterial(engine, glm::vec3(0.9f, 0.75f, 0.5f), 0.25f, 1.0f);

    // Checkered ground along the flight corridor (procedural world-space pattern).
    place("ground", cube, ground, base + glm::dvec3(-120.0, -1.0, -150.0), glm::vec3(static_cast<float>(kCorridorLength), 1.0f, 300.0f), kCheckerShader);
    for (double x = -40.0; x < kCorridorLength - 120.0; x += 40.0) {
        const float height = 4.0f + 6.0f * std::fabs(std::sin(static_cast<float>(x) * 0.013f));
        place("pillar", cube, stone, base + glm::dvec3(x, 0.0, -14.0), glm::vec3(2.5f, height, 2.5f));
        place("pillar", cube, stone, base + glm::dvec3(x + 20.0, 0.0, 12.0), glm::vec3(2.5f, height * 0.7f, 2.5f));
    }
    for (double x = 0.0; x < kCorridorLength - 120.0; x += 120.0) {
        place("sphere", sphere, metal, base + glm::dvec3(x + 8.0, 2.0, -3.0), glm::vec3(3.0f));
        const EntityHandle lamp = world.create([&] {
            EntityDesc description;
            description.name = "lamp";
            description.position = world.toLocal(base + glm::dvec3(x + 10.0, 7.0, 0.0));
            return description;
        }());
        LightComponent light;
        light.type = LightComponent::Type::Point;
        light.color = glm::vec3(1.0f, 0.82f, 0.6f);
        light.intensity = 4.0e5f;
        light.range = 45.0f;
        world.add<LightComponent>(lamp, light);
    }
    const EntityHandle probe = world.create([&] {
        EntityDesc description;
        description.name = "probe";
        description.position = world.toLocal(base + glm::dvec3(6.0, 3.0, 0.0));
        return description;
    }());
    ReflectionProbeComponent probeData;
    probeData.halfExtents = glm::vec3(30.0f, 12.0f, 20.0f);
    world.add<ReflectionProbeComponent>(probe, probeData);

    // Landmark at 20 km plus two depth precision probes: at 50 km a red wall 40 m in front of a wide blue wall, at 100 km the same pair 200 m
    // apart. With standard Z (near 0.1) a 32 bit depth buffer cannot separate either pair; reversed Z can (docs/LargeWorld.md).
    place("landmark 20km", cube, green, base + glm::dvec3(-9000.0, 0.0, -20000.0), glm::vec3(8000.0f, 1800.0f, 600.0f));
    place("wall back 50km", cube, blue, base + glm::dvec3(-30000.0, 0.0, -50080.0), glm::vec3(60000.0f, 8000.0f, 40.0f));
    place("wall front 50km", cube, red, base + glm::dvec3(-10000.0, 0.0, -50000.0), glm::vec3(20000.0f, 12000.0f, 40.0f));
    place("wall back 100km", cube, blue, base + glm::dvec3(25000.0, 0.0, -100200.0), glm::vec3(40000.0f, 30000.0f, 40.0f));
    place("wall front 100km", cube, red, base + glm::dvec3(35000.0, 0.0, -100000.0), glm::vec3(20000.0f, 22000.0f, 40.0f));

    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 14.5f;
    settings.cloudSpeed = 0.0f;  // frozen weather, so screenshots before / after a rebase can be compared
    settings.clouds = true;
    settings.fog = true;
    settings.reflectionMode = ReflectionMode::WorldSpace;
    Camera& camera = world.camera();
    camera.setNearPlane(0.1f);
    camera.setFarPlane(200000.0f);
    gState.start = base + glm::dvec3(0.0, 3.0, 22.0);
    gState.camera = gState.start;
    engine.setCameraWorldPosition(gState.camera);
    camera.setOrientation(-90.0f, 0.0f);
    gState.flying = config.flySpeed > 0.0f;
}

void stepLargeWorldScene(Engine& engine, int frame, float deltaSeconds) {
    if (gState.flying) {
        gState.camera.x += static_cast<double>(gState.config.flySpeed) * static_cast<double>(deltaSeconds);
        engine.setCameraWorldPosition(gState.camera);
    }
    if (gState.config.rebaseFrame > 0 && frame == gState.config.rebaseFrame) {
        const glm::vec3 local = engine.world().camera().getPosition();
        const double snap = 64.0;
        engine.rebaseOrigin(glm::dvec3(std::round(local.x / snap) * snap, std::round(local.y / snap) * snap, std::round(local.z / snap) * snap));
    }
}

void largeWorldCameraWorld(Engine& engine, double out[3]) {
    const glm::dvec3 position = engine.cameraWorldPosition();
    out[0] = position.x;
    out[1] = position.y;
    out[2] = position.z;
}

void setLargeWorldFlight(Engine& engine, float metersPerSecond) {
    gState.config.flySpeed = metersPerSecond;
    gState.flying = metersPerSecond > 0.0f;
    gState.camera = engine.cameraWorldPosition();
}
