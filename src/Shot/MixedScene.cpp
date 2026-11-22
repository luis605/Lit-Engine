#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "Engine/Log/Log.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Rig;
import Engine.Skin;
import Engine.SkinRig;
import Engine.Portal;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.mesh;
import Engine.asset;

// vsm_mixed: everything the integrated renderer has to survive in one frame. The portal "loop" scene (three
// rooms, the camera sees room B through a portal), cluster LOD rocks (330k triangles each), skinned characters
// walking and waving in both rooms, and N shadowed point/spot lights (virtual shadow maps). Animation is
// driven by the frame index so screenshots are repeatable. --lights N (default 4096 is a pool stress).

Mesh makeDenseRock(int subdivisions, float roughness, float seed);

namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;

struct Actor {
    uint32_t instance = kNone;
    EntityHandle entity;
    std::unique_ptr<SkeletalAnimator> animator;
    std::vector<glm::mat4> palette;
};

struct MixedState {
    bool active = false;
    DynamicMesh character;
    Skeleton skeleton;
    SkeletalLibrary clips;
    std::vector<Actor> actors;
    std::vector<std::pair<EntityHandle, glm::vec3>> lights;
};

MixedState gMixed;

float random01(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f;
}

}

void buildVsmMixed(Engine& engine, int lightCount) {
    World& world = engine.world();
    gMixed = {};
    gMixed.active = true;
    const uint32_t cube = engine.loadMesh("cube");
    const uint32_t sphere = engine.loadMesh("sphere");
    auto makeMaterial = [&](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
        Material material;
        material.baseColor = color;
        material.roughness = roughness;
        material.emissive = emissive;
        material.emissiveIntensity = intensity;
        return engine.createMaterial(material);
    };
    PortalSceneKit kit;
    kit.cube = cube;
    kit.sphere = sphere;
    kit.material = makeMaterial;
    buildPortalScene(engine.portals(), world, kit, PortalTestScene::Loop);

    // Cluster LOD rocks (these are the only cluster meshes; the dynamic characters must never become one).
    const uint32_t rock = engine.uploadProcessedMesh(AssetManager::processMesh(makeDenseRock(7, 0.35f, 4.0f)));
    const uint32_t stone = makeMaterial(glm::vec3(0.55f, 0.5f, 0.45f), glm::vec3(0.0f), 0.0f, 0.8f);
    const glm::vec3 rockPositions[] = {glm::vec3(-3.3f, 0.9f, -3.0f), glm::vec3(21.0f, 0.9f, -3.0f), glm::vec3(3.0f, 0.9f, -3.2f)};
    for (const glm::vec3& position : rockPositions) {
        EntityDesc desc;
        desc.name = "cluster rock";
        desc.mesh = rock;
        desc.material = stone;
        desc.position = position;
        desc.scale = glm::vec3(0.9f);
        world.create(desc);
    }

    // Skinned characters: two in room A, two in room B.
    gMixed.character = makeProceduralCharacter();
    gMixed.skeleton = skeletonFromSkin(gMixed.character);
    const uint32_t walk = gMixed.clips.add(makeWalkClip(gMixed.skeleton));
    const uint32_t wave = gMixed.clips.add(makeWaveClip(gMixed.skeleton));
    const uint32_t source = engine.createDynamicSource(gMixed.character);
    const glm::vec3 actorPositions[] = {glm::vec3(0.6f, 0.0f, -0.6f), glm::vec3(-1.5f, 0.0f, 0.2f), glm::vec3(23.0f, 0.0f, 0.5f), glm::vec3(25.5f, 0.0f, -0.8f)};
    const glm::vec3 colors[] = {glm::vec3(0.85f, 0.5f, 0.35f), glm::vec3(0.35f, 0.55f, 0.85f)};
    for (int index = 0; index < 4; ++index) {
        Actor actor;
        actor.instance = source != kNone ? engine.createDynamicInstance(source) : kNone;
        EntityDesc desc;
        desc.name = "skinned actor";
        desc.mesh = actor.instance != kNone ? engine.dynamicMeshId(actor.instance) : 0u;
        desc.material = makeMaterial(colors[index % 2], glm::vec3(0.0f), 0.0f, 0.55f);
        desc.position = actorPositions[index];
        actor.entity = world.create(desc);
        if (actor.instance != kNone) engine.setDynamicEntity(actor.instance, actor.entity);
        actor.animator = std::make_unique<SkeletalAnimator>(gMixed.skeleton, gMixed.clips);
        actor.animator->play(index % 2 == 0 ? walk : wave, 0.0f);
        gMixed.actors.push_back(std::move(actor));
    }

    // Shadowed lights: a strong spot on the characters in room A, then N small ones spread over the three rooms.
    {
        EntityDesc desc;
        desc.name = "actor spot";
        desc.mesh = NO_MESH;
        desc.position = glm::vec3(-2.5f, 3.8f, 3.6f);
        // Forward is -Z: yaw about Y, then pitch about X.
        const glm::vec3 aim = glm::normalize(glm::vec3(0.0f, 1.0f, -0.4f) - desc.position);
        desc.rotation = glm::angleAxis(std::atan2(-aim.x, -aim.z), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(std::asin(aim.y), glm::vec3(1.0f, 0.0f, 0.0f));
        const EntityHandle spot = world.create(desc);
        LightComponent light;
        light.type = LightComponent::Type::Spot;
        light.color = glm::vec3(1.0f, 0.95f, 0.85f);
        light.intensity = 4000000.0f;
        light.range = 14.0f;
        light.innerConeDegrees = 25.0f;
        light.outerConeDegrees = 50.0f;
        world.add<LightComponent>(spot, light);
    }
    uint32_t seed = 777u;
    const glm::vec3 roomCenters[] = {glm::vec3(0.0f), glm::vec3(24.0f, 0.0f, 0.0f), glm::vec3(48.0f, 0.0f, 0.0f)};
    for (int index = 0; index < lightCount; ++index) {
        const glm::vec3 center = roomCenters[index % 3] + glm::vec3((random01(seed) - 0.5f) * 8.5f, 0.6f + 2.8f * random01(seed), (random01(seed) - 0.5f) * 8.5f);
        EntityDesc desc;
        desc.name = "mixed light";
        desc.mesh = NO_MESH;
        desc.position = center;
        const EntityHandle entity = world.create(desc);
        LightComponent light;
        light.type = LightComponent::Type::Point;
        const float hue = random01(seed) * 6.0f;
        light.color = glm::clamp(glm::vec3(std::abs(hue - 3.0f) - 1.0f, 2.0f - std::abs(hue - 2.0f), 2.0f - std::abs(hue - 4.0f)), 0.0f, 1.0f) * 0.7f + 0.3f;
        light.intensity = 40000.0f;
        light.range = 3.0f + 2.5f * random01(seed);
        world.add<LightComponent>(entity, light);
        gMixed.lights.emplace_back(entity, center);
    }
    engine.renderSettings().timeOfDay = 23.0f;

    Camera& camera = world.camera();
    camera.setPos(glm::vec3(-4.2f, 1.8f, 3.8f));
    camera.setOrientation(-25.0f, -8.0f);
    camera.setNearPlane(0.05f);
    camera.setFarPlane(300.0f);
    if (engine.portals().active()) engine.portals().setCameraCell(engine.portals().cellAt(camera.getPosition()));
    Lit::Log::Info("vsm_mixed: {} lights, {} characters, {} cluster rocks", lightCount, gMixed.actors.size(), std::size(rockPositions));
}

void animateVsmMixed(Engine& engine, int frame) {
    if (!gMixed.active) return;
    constexpr float kStep = 1.0f / 60.0f;
    for (Actor& actor : gMixed.actors) {
        actor.animator->update(kStep);
        if (actor.instance == kNone) continue;
        computeSkinPalette(gMixed.skeleton, actor.animator->pose(), gMixed.character, actor.palette);
        DeformSettings none;
        none.recomputeNormals = false;
        engine.setDynamicDeform(actor.instance, none);
        engine.setDynamicPose(actor.instance, actor.palette);
    }
    const float time = static_cast<float>(frame) / 60.0f;
    std::vector<std::pair<EntityHandle, glm::vec3>> updates;
    updates.reserve(gMixed.lights.size() / 4 + 1);
    for (size_t index = 0; index < gMixed.lights.size(); index += 4) {
        const float angle = time * 0.7f + static_cast<float>(index);
        updates.emplace_back(gMixed.lights[index].first, gMixed.lights[index].second + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * 0.6f);
    }
    engine.world().setPositions(updates);
}
