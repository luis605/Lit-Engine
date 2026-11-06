module;

#include <GLFW/glfw3.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <new>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.GalleryScene;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Physics;
import Engine.Character;
import Engine.Replay;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Render.settings;

namespace {

constexpr float kThrowSpeed = 16.0f;
constexpr float kThrownRadius = 0.2f;
constexpr size_t kMaxThrown = 24;
constexpr float kRespawnHeight = -15.0f;
constexpr float kTimePresets[] = {6.5f, 12.0f, 17.5f, 22.0f};

struct PlayState {
    GalleryKit kit;
    uint32_t thrownMaterial = 0;
    std::deque<EntityHandle> thrown;
    int timePreset = 2;
};

void throwSphere(Engine& engine, PlayState& state) {
    CharacterController* character = engine.character();
    World& world = engine.world();
    const glm::vec3 direction = character->rig().forward();
    EntityDesc description;
    description.name = "thrown sphere";
    description.mesh = state.kit.sphere;
    description.material = state.thrownMaterial;
    description.position = character->eye() + direction * 0.8f;
    description.scale = glm::vec3(kThrownRadius);
    const EntityHandle entity = world.create(description);
    world.add<SphereCollider>(entity, SphereCollider{kThrownRadius});
    RigidBody body;
    body.mass = 2.0f;
    body.restitution = 0.5f;
    body.velocity = direction * kThrowSpeed;
    world.add<RigidBody>(entity, body);
    state.thrown.push_back(entity);
    while (state.thrown.size() > kMaxThrown) {
        world.destroy(state.thrown.front());
        state.thrown.pop_front();
    }
}

void cycleTimeOfDay(Engine& engine, PlayState& state) {
    constexpr int presetCount = static_cast<int>(sizeof(kTimePresets) / sizeof(kTimePresets[0]));
    state.timePreset = (state.timePreset + 1) % presetCount;
    engine.renderSettings().timeOfDay = kTimePresets[state.timePreset];
}

void respawnIfFallen(CharacterController& character) {
    if (character.position().y > kRespawnHeight) return;
    const GallerySpawn spawn = gallerySpawn();
    character.spawn(spawn.feet, spawn.yaw, spawn.pitch);
}

}

void enableGalleryPlay(Engine& engine, const GalleryKit& kit) {
    auto state = std::make_shared<PlayState>();
    state->kit = kit;
    Material thrown;
    thrown.baseColor = glm::vec3(0.9f, 0.35f, 0.1f);
    thrown.roughness = 0.3f;
    state->thrownMaterial = kit.material ? kit.material(thrown) : 0;
    CharacterController& character = engine.enableCharacter();
    const GallerySpawn spawn = gallerySpawn();
    character.spawn(spawn.feet, spawn.yaw, spawn.pitch);
    engine.setMouseCaptured(true);
    engine.addSystem(Phase::Update, "gallery play", [&engine, state](World&, float) {
        CharacterController* active = engine.character();
        if (!active) return;
        const SimInput& input = engine.simInput();
        if (input.pressed(GLFW_KEY_ESCAPE)) engine.setMouseCaptured(false);
        if (input.buttonPressed(GLFW_MOUSE_BUTTON_LEFT) && !engine.isMouseCaptured()) engine.setMouseCaptured(true);
        if (input.pressed(GLFW_KEY_F) && engine.isMouseCaptured()) throwSphere(engine, *state);
        if (input.pressed(GLFW_KEY_R)) cycleTimeOfDay(engine, *state);
        respawnIfFallen(*active);
    });
}
