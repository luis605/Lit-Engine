#include "Review3Suites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <limits>
#include <string>

import Engine.Portal;
import Engine.World;
import Engine.glm;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.Character;
import Engine.Render.entity;
import Engine.Render.component;

namespace {

void testSettingsOutOfRange() {
    PortalSystem portals;
    CHECK(portals.parseJson(R"({"settings":{"maxDepth":1e30,"maxViews":-5},"cells":[{"name":"a"}]})"));
    CHECK(portals.settings().maxDepth >= 0);
    CHECK(portals.settings().maxViews >= 0);
}

void testControlCharacterNamesRoundTrip() {
    PortalSystem portals;
    CHECK(portals.parseJson(R"({"cells":[{"name":"line\nbreak"},{"name":"b"}],"portals":[{"name":"p\tq","a":{"cell":"line\nbreak"},"b":{"cell":"b"}}]})"));
    const std::string dumped = portals.toJson();
    PortalSystem reloaded;
    CHECK(reloaded.parseJson(dumped));
    CHECK(reloaded.cells().size() == 2);
    CHECK(reloaded.findCell("line\nbreak") == 0);
}

void testMalformedDocuments() {
    PortalSystem portals;
    CHECK(!portals.parseJson("{"));
    CHECK(!portals.parseJson("[]"));
    CHECK(!portals.parseJson(R"({"cells":[]})"));
    CHECK(!portals.parseJson(R"({"cells":[{"name":"a"},{"name":"a"}]})"));
    CHECK(!portals.parseJson(R"({"cells":[{"name":"a"}],"portals":[{"a":{"cell":"a"},"b":{"cell":"zzz"}}]})"));
    CHECK(!portals.parseJson(R"({"cells":[{"name":"a"}],"portals":[{"a":{"cell":"a"}}]})"));
    CHECK(!portals.active());
}

void testZeroSizeAndHugeCrossings() {
    PortalSystem portals;
    CHECK(portals.parseJson(R"({"cells":[{"name":"a"},{"name":"b"}],"portals":[{"name":"p","a":{"cell":"a","size":[0,0]},"b":{"cell":"b","size":[0,0]}}]})"));
    World world;
    const PortalRayHit hit = portals.raycast(world, glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f), 1.0e9f, 0, 2000000000);
    CHECK(hit.crossings <= 300);
    const PortalRayHit nan = portals.raycast(world, glm::vec3(0.0f), glm::vec3(0.0f), 10.0f, 0, 3);
    CHECK(nan.crossings == 0);
}


void testCharacterHostileInput() {
    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    PortalSystem portals;
    PhysicsSettings physics;
    CharacterSettings settings;
    settings.substep = 0.0f;
    CharacterController character(world, portals, physics, settings);
    character.spawn(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
    character.spawn(glm::vec3(kNan, 0.0f, 0.0f), 0.0f);
    character.look(kNan, kNan);
    CharacterInput input;
    input.move = glm::vec2(kNan, 1.0f);
    character.setInput(input);
    character.update(kNan);
    character.update(std::numeric_limits<float>::infinity());
    character.update(1.0f / 60.0f);
    character.update(1000.0f);
    const glm::vec3 feet = character.position();
    CHECK(std::isfinite(feet.x) && std::isfinite(feet.y) && std::isfinite(feet.z));
    CHECK(std::isfinite(character.yaw()) && std::isfinite(character.pitch()));
}

}

void runReview3PortalTests() {
    testCharacterHostileInput();
    testSettingsOutOfRange();
    testControlCharacterNamesRoundTrip();
    testMalformedDocuments();
    testZeroSizeAndHugeCrossings();
}
