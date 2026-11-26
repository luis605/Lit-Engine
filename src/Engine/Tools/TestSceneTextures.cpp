module;

#include <cstdint>
#include <string>
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

constexpr const char* kAlbedoPath = "resources/textures/test/bricks_albedo.png";
constexpr const char* kNormalPath = "resources/textures/test/bricks_normal.png";
constexpr const char* kOrmPath = "resources/textures/test/bricks_orm.png";

EntityHandle placeTextured(World& world, const char* name, uint32_t mesh, uint32_t material, const glm::vec3& position, const glm::vec3& scale) {
    EntityDesc description;
    description.name = name;
    description.mesh = mesh;
    description.material = material;
    description.position = position;
    description.scale = scale;
    return world.create(description);
}

}

bool buildTexturesTestScene(Engine& engine, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params) {
    World& world = engine.world();
    int albedo = -1;
    int normal = -1;
    int orm = -1;
    if (!engine.headless()) {
        const int scratch = engine.loadTexture(kOrmPath, true);
        engine.releaseTexture(scratch, true);
        albedo = engine.loadTexture(kAlbedoPath, true);
        normal = engine.loadTexture(kNormalPath, false);
        orm = engine.loadTexture(kOrmPath, false);
    }
    auto textured = [&](const glm::vec3& tint, bool useAlbedo, bool useNormal, bool useOrm, bool emissive, bool triplanar, float scale) {
        Material material;
        material.baseColor = tint;
        material.roughness = 0.8f;
        material.uvScale = scale;
        material.triplanar = triplanar;
        if (useAlbedo) material.baseColorTexture = albedo;
        if (useNormal) material.normalTexture = normal;
        if (useOrm) material.ormTexture = orm;
        if (emissive) {
            material.emissiveTexture = albedo;
            material.emissiveIntensity = 0.6f;
        }
        return testSceneMaterial(engine, kit, material);
    };
    const uint32_t floor = textured(glm::vec3(1.0f), true, true, true, false, false, 6.0f);
    placeTextured(world, "floor", kit.cube, floor, glm::vec3(-8.0f, -0.5f, -8.0f), glm::vec3(16.0f, 0.5f, 16.0f));
    struct Sample {
        const char* name;
        bool albedo, normal, orm, emissive, triplanar;
    };
    const Sample samples[] = {
        {"albedo",    true,  false, false, false, false},
        {"normal",    false, true,  false, false, false},
        {"albedo+normal", true, true, false, false, false},
        {"albedo+normal+orm", true, true, true, false, false},
        {"emissive",  false, false, false, true,  false},
        {"triplanar", true,  true,  true,  false, true },
    };
    float x = -5.0f;
    for (const Sample& sample : samples) {
        const uint32_t material = textured(glm::vec3(1.0f), sample.albedo, sample.normal, sample.orm, sample.emissive, sample.triplanar, 2.0f);
        placeTextured(world, sample.name, kit.sphere, material, glm::vec3(x, 1.0f, 0.0f), glm::vec3(1.8f));
        x += 2.0f;
    }
    LightComponent lamp;
    lamp.type = LightComponent::Type::Point;
    lamp.color = glm::vec3(1.0f, 0.9f, 0.75f);
    lamp.intensity = params.lightIntensity;
    lamp.range = 20.0f;
    lamp.sourceRadius = 0.2f;
    world.add<LightComponent>(placeTextured(world, "lamp", NO_MESH, 0, glm::vec3(0.0f, 3.5f, 2.5f), glm::vec3(1.0f)), lamp);
    info.start = glm::vec3(0.0f, 2.2f, 8.5f);
    info.yaw = -90.0f;
    info.pitch = -12.0f;
    info.nearPlane = 0.1f;
    info.farPlane = 200.0f;
    info.suggestedTicks = 60;
    return true;
}
