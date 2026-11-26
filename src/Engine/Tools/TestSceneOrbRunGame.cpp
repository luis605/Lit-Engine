module;

#include <GLFW/glfw3.h>
#include <cmath>
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
import Engine.Character;
import Engine.camera;
import Engine.Replay;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr float kOrbBobAmplitude = 0.12f;
constexpr float kOrbBobSpeed = 2.0f;
constexpr float kFallRespawnHeight = -15.0f;
constexpr float kCrateFallHeight = -6.0f;
constexpr float kShoveSpeed = 5.0f;
constexpr float kShoveLift = 1.0f;
constexpr float kShoveReach = 3.5f;
constexpr float kShoveAlignment = 0.6f;

void animateOrbs(World& world, OrbRunGame& game) {
    const float time = static_cast<float>(world.time().elapsed);
    for (size_t index = 0; index < game.orbs.size(); ++index) {
        const OrbRunOrb& orb = game.orbs[index];
        if (!world.isAlive(orb.entity)) continue;
        world.setPosition(orb.entity, orb.home + glm::vec3(0.0f, kOrbBobAmplitude * std::sin(kOrbBobSpeed * time + static_cast<float>(index)), 0.0f));
    }
}

void collectOrbs(World& world, OrbRunGame& game, const PlayerPose& pose) {
    const int picked = orbRunPickupIndex(game.state, game.config, game.layout.orbs, pose.center);
    if (picked < 0 || !orbRunCollect(game.state, game.config, picked)) return;
    OrbRunOrb& orb = game.orbs[static_cast<size_t>(picked)];
    if (world.isAlive(orb.entity)) world.destroy(orb.entity);
    const size_t socket = static_cast<size_t>(game.state.collected - 1);
    if (socket < game.sockets.size()) world.setMaterial(game.sockets[socket], game.socketLit);
}

void resolveSpiderContact(Engine& engine, OrbRunGame& game, const PlayerPose& pose) {
    World& world = engine.world();
    for (const OrbRunSpider& spider : game.spiders) {
        if (!orbRunTouches(game.config, pose.center, world.getWorldPosition(spider.body))) continue;
        if (!orbRunHit(game.state, game.config)) return;
        ++game.spiderHits;
        respawnOrbRunPlayer(engine);
        return;
    }
}

void shoveCrate(Engine& engine, OrbRunGame& game, const PlayerPose& pose) {
    World& world = engine.world();
    OrbRunCrate* best = nullptr;
    float bestDistance = kShoveReach;
    for (OrbRunCrate& crate : game.crates) {
        if (!world.isAlive(crate.entity)) continue;
        const glm::vec3 offset = world.getWorldPosition(crate.entity) - pose.eye;
        const float distance = glm::length(offset);
        if (distance >= bestDistance || glm::dot(offset / distance, pose.forward) < kShoveAlignment) continue;
        bestDistance = distance;
        best = &crate;
    }
    if (!best) return;
    if (RigidBody* body = world.get<RigidBody>(best->entity)) {
        body->velocity = pose.forward * kShoveSpeed + glm::vec3(0.0f, kShoveLift, 0.0f);
        body->sleeping = false;
    }
}

void recoverFallenCrates(World& world, OrbRunGame& game) {
    for (const OrbRunCrate& crate : game.crates) {
        if (!world.isAlive(crate.entity) || world.getWorldPosition(crate.entity).y > kCrateFallHeight) continue;
        world.setWorldPosition(crate.entity, crate.home);
        if (RigidBody* body = world.get<RigidBody>(crate.entity)) body->velocity = glm::vec3(0.0f);
    }
}

void handlePlayInput(Engine& engine, OrbRunGame& game, const PlayerPose& pose) {
    const SimInput& input = engine.simInput();
    if (input.pressed(GLFW_KEY_ESCAPE)) engine.setMouseCaptured(false);
    if (input.buttonPressed(GLFW_MOUSE_BUTTON_LEFT) && !engine.isMouseCaptured()) engine.setMouseCaptured(true);
    if (input.pressed(GLFW_KEY_E) && engine.isMouseCaptured()) shoveCrate(engine, game, pose);
}

}

PlayerPose playerPose(Engine& engine) {
    PlayerPose pose;
    if (CharacterController* character = engine.character()) {
        pose.eye = character->eye();
        pose.forward = character->rig().forward();
        pose.feet = character->position();
    } else {
        const Camera& camera = engine.world().camera();
        pose.eye = camera.getPosition();
        pose.forward = camera.getFront();
        pose.feet = pose.eye - glm::vec3(0.0f, kEyeHeight, 0.0f);
    }
    pose.center = pose.feet + glm::vec3(0.0f, kBodyCenterHeight, 0.0f);
    pose.cell = engine.portals().cameraCell();
    return pose;
}

void tickGame(Engine& engine, OrbRunGame& game, float deltaTime) {
    World& world = engine.world();
    const PlayerPose pose = playerPose(engine);
    if (game.playable) {
        if (engine.simInput().pressed(GLFW_KEY_R)) {
            restartOrbRun(engine);
            return;
        }
        handlePlayInput(engine, game, pose);
        if (pose.feet.y < kFallRespawnHeight) respawnOrbRunPlayer(engine);
    }
    animateOrbs(world, game);
    recoverFallenCrates(world, game);
    steerSpiders(engine, game, pose);
    if (game.state.phase != OrbRunPhase::Playing) return;
    orbRunAdvance(game.state, deltaTime);
    collectOrbs(world, game, pose);
    resolveSpiderContact(engine, game, pose);
}

}

void respawnOrbRunPlayer(Engine& engine) {
    OrbRunGame* game = orbRunGame(engine);
    if (!game) return;
    const OrbRunSpawn& spawn = game->layout.spawn;
    if (CharacterController* character = engine.character()) {
        character->spawn(spawn.feet, spawn.yaw, spawn.pitch);
    } else {
        Camera& camera = engine.world().camera();
        camera.setPos(spawn.feet + glm::vec3(0.0f, orbrun::kEyeHeight, 0.0f));
        camera.setOrientation(spawn.yaw, spawn.pitch);
        engine.portals().setCameraCell(engine.portals().cellAt(camera.getPosition()));
    }
    ++game->respawns;
}

void restartOrbRun(Engine& engine) {
    OrbRunGame* game = orbRunGame(engine);
    if (!game) return;
    World& world = engine.world();
    game->state = orbRunStart(game->config);
    for (OrbRunOrb& orb : game->orbs) {
        if (!world.isAlive(orb.entity)) orbrun::spawnOrb(world, game->orbMesh, orb);
    }
    for (const EntityHandle socket : game->sockets) world.setMaterial(socket, game->socketDim);
    for (const OrbRunCrate& crate : game->crates) {
        if (!world.isAlive(crate.entity)) continue;
        world.setWorldPosition(crate.entity, crate.home);
        if (RigidBody* body = world.get<RigidBody>(crate.entity)) body->velocity = glm::vec3(0.0f);
    }
    for (OrbRunSpider& spider : game->spiders) spider.waypoint = 0;
    respawnOrbRunPlayer(engine);
    game->respawns = 0;
    game->spiderHits = 0;
}
