module;

#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr float kOrbRadius = 0.3f;
constexpr float kOrbLightIntensity = 6000.0f;
constexpr float kOrbLightRange = 10.0f;
constexpr float kOrbEmission = 1500.0f;
constexpr float kCrateHalf = 0.4f;
constexpr float kCrateMass = 8.0f;
constexpr float kCrateFriction = 0.7f;

const glm::vec3 kOrbColors[] = {
    {1.0f, 0.78f, 0.2f}, {0.2f, 0.9f, 1.0f}, {1.0f, 0.25f, 0.8f}, {0.4f, 1.0f, 0.35f},
    {1.0f, 0.5f, 0.15f}, {0.35f, 0.5f, 1.0f}, {0.95f, 0.95f, 1.0f}, {1.0f, 0.3f, 0.3f},
};

}

void spawnOrb(World& world, uint32_t sphereMesh, uint32_t shader, OrbRunOrb& orb) {
    EntityDesc description;
    description.name = "orb";
    description.mesh = sphereMesh;
    description.material = orb.material;
    description.position = orb.home;
    description.scale = glm::vec3(kOrbRadius);
    description.shader = shader;
    orb.entity = world.create(description);
    LightComponent light;
    light.type = LightComponent::Type::Point;
    light.color = orb.color;
    light.intensity = kOrbLightIntensity;
    light.range = kOrbLightRange;
    light.sourceRadius = kOrbRadius;
    world.add<LightComponent>(orb.entity, light);
}

void buildOrbs(const Context& context) {
    context.game.orbs.clear();
    context.game.orbMesh = context.kit.portal.sphere;
    context.game.orbShader = context.looks.orb;
    const size_t colorCount = sizeof(kOrbColors) / sizeof(kOrbColors[0]);
    for (size_t index = 0; index < context.game.layout.orbs.size(); ++index) {
        OrbRunOrb orb;
        orb.home = context.game.layout.orbs[index];
        orb.color = kOrbColors[index % colorCount];
        orb.material = context.glow(orb.color, kOrbEmission);
        spawnOrb(context.world, context.game.orbMesh, context.game.orbShader, orb);
        context.game.orbs.push_back(orb);
    }
}

void buildCrates(const Context& context) {
    const uint32_t wood = context.surfaced(Finish::Wood, glm::vec3(0.72f, 0.5f, 0.28f), 0.75f, {.tileMeters = 1.0f, .worldAligned = false});
    const uint32_t frame = context.surfaced(Finish::Wood, glm::vec3(0.34f, 0.22f, 0.13f), 0.6f, {.tileMeters = 0.7f, .worldAligned = false});
    const uint32_t panel = context.mesh("crate.panel", roundedBoxMesh(glm::vec3(kCrateHalf * 0.95f), 0.02f, 0.0f));
    const uint32_t braces = context.mesh("crate.frame", crateFrameMesh(kCrateHalf * 0.97f));
    context.game.crates.clear();
    for (const glm::vec3& home : context.game.layout.crates) {
        EntityDesc body;
        body.name = "crate";
        body.position = home;
        const EntityHandle crate = context.world.create(body);
        context.world.setTag(crate, "orbrun.crate");
        context.world.add<BoxCollider>(crate, BoxCollider{glm::vec3(kCrateHalf)});
        RigidBody physics;
        physics.mass = kCrateMass;
        physics.friction = kCrateFriction;
        context.world.add<RigidBody>(crate, physics);
        EntityDesc skin;
        skin.name = "crate skin";
        skin.mesh = panel;
        skin.material = wood;
        skin.parent = crate;
        context.world.create(skin);
        skin.name = "crate braces";
        skin.mesh = braces;
        skin.material = frame;
        context.world.create(skin);
        context.game.crates.push_back({crate, home});
    }
}

void buildPortals(const Context& context) {
    PortalSystem& portals = context.engine.portals();
    const glm::vec3 hubTint(0.9f, 0.8f, 0.6f);
    struct CellSpec {
        Room room;
        glm::vec3 tint;
        float sun;
        float ambient;
    };
    const CellSpec cells[] = {
        {hubRoom(), hubTint, 0.0f, 0.12f},
        {emberRoom(), glm::vec3(1.0f, 0.45f, 0.2f), 0.0f, 0.1f},
        {frostRoom(), glm::vec3(0.45f, 0.75f, 1.0f), 0.0f, 0.1f},
        {grottoRoom(), glm::vec3(0.5f, 1.0f, 0.55f), 0.0f, 0.1f},
    };
    for (const CellSpec& spec : cells) {
        PortalCell cell;
        cell.name = spec.room.name;
        cell.min = interiorMin(spec.room) - glm::vec3(kCellMargin, kCellMargin, kCellMargin);
        cell.max = interiorMax(spec.room) + glm::vec3(kCellMargin);
        cell.params.tint = spec.tint * 0.3f;
        cell.params.sun = spec.sun;
        cell.params.ambient = spec.ambient;
        portals.addCell(cell);
    }
    PortalCell courtyard;
    courtyard.name = kOrbRunCourtyard;
    courtyard.min = glm::vec3(kCourtyardX - kPlazaHalf - 2.0f, -6.0f, -kPlazaHalf - 2.0f);
    courtyard.max = glm::vec3(kCourtyardX + kPlazaHalf + 2.0f, 40.0f, kPlazaHalf + 2.0f);
    courtyard.params.tint = glm::vec3(0.1f);
    portals.addCell(courtyard);
    for (const PairSpec& pair : portalPairs()) portals.addPair(pair.name, pair.a, pair.b, 0.0f, pair.look);
    buildPortalFrames(portals, context.world, context.kit.portal);
}

}
