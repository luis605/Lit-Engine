module;

#include <GLFW/glfw3.h>
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
import Engine.glm;
import Engine.World;
import Engine.Physics;
import Engine.Character;
import Engine.IK;
import Engine.Replay;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Render.settings;

namespace {

constexpr float kRespawnHeight = -15.0f;
constexpr float kSpiderFollowDistance = 3.0f;
constexpr float kPatrolArrival = 0.6f;
constexpr float kPushableFallHeight = -6.0f;
constexpr float kGrabDistance = 6.0f;
constexpr float kGrabHoldDistance = 2.2f;
constexpr float kTimePresets[] = {6.5f, 12.0f, 16.5f, 22.0f};

struct PushableHome {
    EntityHandle entity;
    glm::vec3 home{0.0f};
};

struct PlayState {
    std::vector<PushableHome> pushables;
    EntityHandle grabbed;
    size_t patrolIndex = 0;
    int timePreset = 2;
    bool lookInitialized = false;
};

void collectPushables(World& world, PlayState& state) {
    state.pushables.clear();
    world.view<SphereCollider>([&](EntityHandle entity, SphereCollider&) {
        if (world.getTag(entity) == kIkTestPushableTag) state.pushables.push_back({entity, world.getWorldPosition(entity)});
    });
}

void respawnFallenPushables(World& world, PlayState& state) {
    for (const PushableHome& pushable : state.pushables) {
        if (!world.isAlive(pushable.entity) || world.getWorldPosition(pushable.entity).y > kPushableFallHeight) continue;
        world.setPosition(pushable.entity, pushable.home);
        if (RigidBody* body = world.get<RigidBody>(pushable.entity)) body->velocity = glm::vec3(0.0f);
    }
}

void steerSpider(World& world, const CharacterController& character) {
    const EntityHandle spider = world.find(kIkTestSpiderName);
    if (!world.isAlive(spider)) return;
    const glm::vec3 spiderPosition = world.getWorldPosition(spider);
    glm::vec3 away = spiderPosition - character.position();
    away.y = 0.0f;
    const float distance = glm::length(away);
    const glm::vec3 direction = distance > 0.001f ? away / distance : glm::vec3(0.0f, 0.0f, 1.0f);
    setWalkerTarget(world, spider, character.position() + direction * kSpiderFollowDistance);
}

void steerPatrol(World& world, PlayState& state) {
    const EntityHandle quadruped = world.find(kIkTestQuadrupedName);
    if (!world.isAlive(quadruped)) return;
    const EntityHandle waypoint = world.find(kIkTestPatrolPrefix + std::to_string(state.patrolIndex));
    if (!world.isAlive(waypoint)) {
        state.patrolIndex = 0;
        return;
    }
    const glm::vec3 goal = world.getWorldPosition(waypoint);
    glm::vec3 offset = goal - world.getWorldPosition(quadruped);
    offset.y = 0.0f;
    if (glm::length(offset) < kPatrolArrival) ++state.patrolIndex;
    setWalkerTarget(world, quadruped, goal);
}

void steerBiped(World& world) {
    const EntityHandle biped = world.find(kIkTestBipedName);
    const EntityHandle goal = world.find(kIkTestBipedGoalName);
    if (world.isAlive(biped) && world.isAlive(goal)) setWalkerTarget(world, biped, world.getWorldPosition(goal));
}

void aimLookTarget(World& world, const CharacterController& character) {
    const EntityHandle target = world.find(kIkTestLookTargetName);
    if (world.isAlive(target)) world.setPosition(target, character.eye());
}

void holdGrabbed(World& world, CharacterController& character, PlayState& state) {
    if (!world.isAlive(state.grabbed)) return;
    const glm::vec3 wanted = character.eye() + character.rig().forward() * kGrabHoldDistance;
    world.setPosition(state.grabbed, wanted);
    if (RigidBody* body = world.get<RigidBody>(state.grabbed)) body->velocity = glm::vec3(0.0f);
}

void toggleGrab(World& world, CharacterController& character, PlayState& state) {
    if (world.isAlive(state.grabbed)) {
        state.grabbed = EntityHandle{};
        return;
    }
    float best = kGrabDistance;
    for (const PushableHome& pushable : state.pushables) {
        if (!world.isAlive(pushable.entity)) continue;
        const glm::vec3 offset = world.getWorldPosition(pushable.entity) - character.eye();
        const float distance = glm::length(offset);
        if (distance >= best || glm::dot(offset / distance, character.rig().forward()) < 0.8f) continue;
        best = distance;
        state.grabbed = pushable.entity;
    }
}

void respawnIfFallen(CharacterController& character) {
    if (character.position().y > kRespawnHeight) return;
    const IkTestSpawn spawn = ikTestSpawn();
    character.spawn(spawn.feet, spawn.yaw, spawn.pitch);
}

void cycleTimeOfDay(Engine& engine, PlayState& state) {
    constexpr int presetCount = static_cast<int>(sizeof(kTimePresets) / sizeof(kTimePresets[0]));
    state.timePreset = (state.timePreset + 1) % presetCount;
    engine.renderSettings().timeOfDay = kTimePresets[state.timePreset];
}

}

void enableIkTestPlay(Engine& engine, const IkTestKit&) {
    auto state = std::make_shared<PlayState>();
    collectPushables(engine.world(), *state);
    CharacterController& character = engine.enableCharacter();
    const IkTestSpawn spawn = ikTestSpawn();
    character.spawn(spawn.feet, spawn.yaw, spawn.pitch);
    engine.setMouseCaptured(true);
    engine.addSystem(Phase::Update, "ik test play", [&engine, state](World& world, float) {
        CharacterController* active = engine.character();
        if (!active) return;
        const SimInput& input = engine.simInput();
        if (input.pressed(GLFW_KEY_ESCAPE)) engine.setMouseCaptured(false);
        if (input.buttonPressed(GLFW_MOUSE_BUTTON_LEFT) && !engine.isMouseCaptured()) engine.setMouseCaptured(true);
        if (input.pressed(GLFW_KEY_E) && engine.isMouseCaptured()) toggleGrab(world, *active, *state);
        if (input.pressed(GLFW_KEY_R)) cycleTimeOfDay(engine, *state);
        holdGrabbed(world, *active, *state);
        steerSpider(world, *active);
        steerPatrol(world, *state);
        steerBiped(world);
        aimLookTarget(world, *active);
        respawnFallenPushables(world, *state);
        respawnIfFallen(*active);
    });
}
