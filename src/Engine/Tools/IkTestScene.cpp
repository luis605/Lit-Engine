module;

#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.IkTestScene;

import Engine.engine;
import Engine.Physics;
import Engine.glm;
import Engine.camera;
import Engine.Render.component;
import Engine.Render.entity;
import Engine.World;
import Engine.IK;
import Engine.Render.material;
import Engine.Render.settings;

namespace {

constexpr float kPushableMass = 3.0f;
constexpr float kPushableRestitution = 0.25f;

glm::quat aimAt(const glm::vec3& forward) {
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);
    return glm::quat_cast(glm::mat3(right, up, -forward));
}

void addLight(World& world, const char* name, const LightComponent& component, const glm::vec3& position, const glm::vec3& target) {
    EntityDesc description;
    description.name = name;
    description.position = position;
    if (target != position) description.rotation = aimAt(glm::normalize(target - position));
    world.add<LightComponent>(world.create(description), component);
}

void buildLighting(World& world) {
    LightComponent fill;
    fill.type = LightComponent::Type::Point;
    fill.color = glm::vec3(1.0f, 0.8f, 0.6f);
    fill.intensity = 9000000.0f;
    fill.range = 40.0f;
    fill.sourceRadius = 0.4f;
    addLight(world, "warm fill", fill, glm::vec3(-6.0f, 5.0f, 8.0f), glm::vec3(-6.0f, 5.0f, 8.0f));

    LightComponent cool = fill;
    cool.color = glm::vec3(0.6f, 0.75f, 1.0f);
    cool.intensity = 7000000.0f;
    addLight(world, "cool fill", cool, glm::vec3(10.0f, 5.5f, -6.0f), glm::vec3(10.0f, 5.5f, -6.0f));

    LightComponent spot;
    spot.type = LightComponent::Type::Spot;
    spot.color = glm::vec3(1.0f, 0.9f, 0.75f);
    spot.intensity = 26000000.0f;
    spot.range = 45.0f;
    spot.innerConeDegrees = 18.0f;
    spot.outerConeDegrees = 40.0f;
    spot.sourceRadius = 0.25f;
    addLight(world, "spider spot", spot, glm::vec3(2.0f, 9.0f, 9.0f), glm::vec3(-2.0f, 0.0f, 1.0f));
}

}

uint32_t ikTestPaint(const IkTestKit& kit, const glm::vec3& color, float roughness, float metallic) {
    Material material;
    material.baseColor = color;
    material.roughness = roughness;
    material.metallic = metallic;
    return kit.material ? kit.material(material) : 0;
}

uint32_t ikTestGlow(const IkTestKit& kit, const glm::vec3& color, float intensity) {
    Material material;
    material.baseColor = color;
    material.emissive = color;
    material.emissiveIntensity = intensity;
    return kit.material ? kit.material(material) : 0;
}

EntityHandle ikTestStaticBox(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& minCorner, const glm::vec3& size) {
    EntityDesc colliderDescription;
    colliderDescription.name = name;
    colliderDescription.position = minCorner + size * 0.5f;
    const EntityHandle collider = world.create(colliderDescription);
    world.add<BoxCollider>(collider, BoxCollider{size * 0.5f});
    RigidBody body;
    body.isStatic = true;
    world.add<RigidBody>(collider, body);
    EntityDesc visual;
    visual.name = std::string(name) + ".mesh";
    visual.mesh = kit.cube;
    visual.material = material;
    visual.parent = collider;
    visual.position = -size * 0.5f;
    visual.scale = size;
    world.create(visual);
    return collider;
}

EntityHandle ikTestStaticBump(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& center, float radius) {
    EntityDesc description;
    description.name = name;
    description.mesh = kit.sphere;
    description.material = material;
    description.position = center;
    description.scale = glm::vec3(radius);
    const EntityHandle entity = world.create(description);
    world.add<SphereCollider>(entity, SphereCollider{radius});
    RigidBody body;
    body.isStatic = true;
    world.add<RigidBody>(entity, body);
    return entity;
}

EntityHandle ikTestPushableSphere(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& center, float radius) {
    EntityDesc description;
    description.name = name;
    description.mesh = kit.sphere;
    description.material = material;
    description.position = center;
    description.scale = glm::vec3(radius);
    const EntityHandle entity = world.create(description);
    world.add<SphereCollider>(entity, SphereCollider{radius});
    RigidBody body;
    body.mass = kPushableMass;
    body.restitution = kPushableRestitution;
    world.add<RigidBody>(entity, body);
    world.setTag(entity, kIkTestPushableTag);
    return entity;
}

IkTestSpawn ikTestSpawn() {
    IkTestSpawn spawn;
    spawn.feet = glm::vec3(0.0f, 0.05f, 15.0f);
    spawn.yaw = -90.0f;
    spawn.pitch = -8.0f;
    return spawn;
}

void buildIkTestScene(Engine& engine, const IkTestKit& kit) {
    World& world = engine.world();
    buildIkTestTerrain(world, kit);
    buildIkTestRigs(world, kit);
    buildLighting(world);
    if (kit.debugDraw) {
        engine.addSystem(Phase::PostUpdate, "ik test debug", [&engine](World& scene, float) {
            drawIkDebug(scene, [&engine](const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) { engine.debugLine(from, to, color, true); });
        });
    }
    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 16.5f;
    settings.sunAzimuth = 130.0f;
    settings.clouds = true;
    settings.cloudCoverage = 0.4f;
    Camera& camera = world.camera();
    camera.setPos(glm::vec3(0.0f, 6.5f, 22.0f));
    camera.setOrientation(-90.0f, -17.0f);
    camera.setNearPlane(0.1f);
    camera.setFarPlane(400.0f);
}
