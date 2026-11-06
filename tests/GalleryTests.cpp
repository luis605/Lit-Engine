#include "TestSupport.hpp"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <memory>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.Character;
import Engine.Replay;
import Engine.Render.material;
import Engine.Render.entity;
import Engine.glm;
import Engine.GalleryScene;

namespace {

constexpr float kStep = 1.0f / 60.0f;

std::unique_ptr<Engine> makeGallery() {
    auto engine = std::make_unique<Engine>();
    engine->openHeadless();
    GalleryKit kit;
    kit.colliders = true;
    kit.material = [](const Material&) { return 0u; };
    buildGalleryScene(*engine, kit);
    enableGalleryPlay(*engine, kit);
    return engine;
}

void walkForward(Engine& engine, int ticks) {
    engine.runHeadless(static_cast<uint64_t>(ticks), kStep, [&engine](uint64_t) {
        InputState state;
        state.keys.set(GLFW_KEY_W);
        engine.setInput(state);
    });
}

void walkFrom(Engine& engine, const glm::vec3& feet, float yaw, int ticks) {
    engine.character()->spawn(feet, yaw);
    walkForward(engine, ticks);
}

void testSpawnStaysOnFloor() {
    auto engine = makeGallery();
    engine->runHeadless(120, kStep);
    const CharacterController& character = *engine->character();
    CHECK(character.grounded());
    CHECK(character.position().y > -0.05f && character.position().y < 0.2f);
    CHECK(engine->isMouseCaptured());
}

void testWalkProgressesOverPlinth() {
    auto engine = makeGallery();
    walkForward(*engine, 240);
    const CharacterController& character = *engine->character();
    CHECK(character.position().z < 8.0f);
    CHECK(character.position().y > -0.05f);
    CHECK(character.grounded());
}

void testLeftWallBlocks() {
    auto engine = makeGallery();
    walkFrom(*engine, glm::vec3(-5.0f, 0.05f, -4.0f), 180.0f, 600);
    const CharacterController& character = *engine->character();
    CHECK(character.position().x > -12.0f);
    CHECK(character.position().x < -11.0f);
    CHECK(character.position().y > -0.05f);
}

void testRightWallBlocks() {
    auto engine = makeGallery();
    walkFrom(*engine, glm::vec3(5.0f, 0.05f, -4.0f), 0.0f, 600);
    const CharacterController& character = *engine->character();
    CHECK(character.position().x < 12.0f);
    CHECK(character.position().x > 11.0f);
}

void testBackWallBlocks() {
    auto engine = makeGallery();
    walkFrom(*engine, glm::vec3(3.5f, 0.05f, -2.0f), -90.0f, 600);
    const CharacterController& character = *engine->character();
    CHECK(character.position().z > -12.0f);
    CHECK(character.position().z < -11.0f);
}

void testColumnBlocks() {
    auto engine = makeGallery();
    walkFrom(*engine, glm::vec3(7.4f, 0.05f, -4.0f), -90.0f, 600);
    const CharacterController& character = *engine->character();
    CHECK(character.position().z > -8.0f);
    CHECK(character.position().z < -7.5f);
}

void testArchColumnBlocks() {
    auto engine = makeGallery();
    walkFrom(*engine, glm::vec3(2.15f, 0.05f, -2.0f), -90.0f, 600);
    const CharacterController& character = *engine->character();
    CHECK(character.position().z > -7.6f);
}

void testDynamicSpheresStayAboveFloor() {
    auto engine = makeGallery();
    walkForward(*engine, 600);
    int bodies = 0;
    bool allAbove = true;
    World& world = engine->world();
    world.view<RigidBody>([&](EntityHandle entity, RigidBody& body) {
        if (body.isStatic) return;
        ++bodies;
        if (world.getPosition(entity).y < 0.0f) allAbove = false;
    });
    CHECK(bodies >= 20);
    CHECK(allAbove);
}

void testFallRespawns() {
    auto engine = makeGallery();
    engine->runHeadless(5, kStep);
    engine->character()->spawn(glm::vec3(0.0f, -30.0f, 40.0f), 0.0f);
    engine->runHeadless(5, kStep);
    CHECK(engine->character()->position().y > -1.0f);
}

}

int main() {
    testSpawnStaysOnFloor();
    testWalkProgressesOverPlinth();
    testLeftWallBlocks();
    testRightWallBlocks();
    testBackWallBlocks();
    testColumnBlocks();
    testArchColumnBlocks();
    testDynamicSpheresStayAboveFloor();
    testFallRespawns();
    return testsupport::finish();
}
