#include "TestSupport.hpp"
#include "NarrativeFixtures.hpp"
#include "NarrativeSuites.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.camera;
import Engine.Events;
import Engine.Rng;
import Engine.Json;
import Engine.Params;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Save;
import Engine.CameraRig;

namespace {

void testCameraRig() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    const EntityHandle player = w.create("player", 1, {0.0f, 0.0f, 0.0f}, {3.0f, 3.0f, 3.0f});
    w.camera().setOrientation(-90.0f, 0.0f);
    CameraRig rig(w);
    rig.setTarget(player);
    rig.update();
    const glm::vec3 eye(0.0f, 1.6f, 0.0f);
    CHECK(glm::length(w.camera().getPosition() - eye) < 1e-5f);

    CHECK(rig.toggle() == CameraMode::ThirdPerson);
    CHECK(std::abs(w.camera().getPosition().z - 3.5f) < 1e-4f);
    CHECK(glm::length(rig.pivot() - eye) < 1e-5f);
    const glm::vec3 fpFwd = rig.forward();
    const ObservationView v = observationViewFromCamera(w.camera(), 1.0f);
    CHECK(glm::dot(v.forward, fpFwd) > 0.9999f);

    const EntityHandle wall = w.create("wall", 1, {0.0f, 1.6f, 4.5f});
    CHECK(rig.toggle() == CameraMode::FirstPerson);
    CHECK(glm::length(w.camera().getPosition() - eye) < 1e-5f);
    rig.toggle();
    CHECK(rig.currentDistance() < 3.5f);
    CHECK(std::abs(rig.currentDistance() - (3.5f - 0.25f)) < 0.3f);
    CHECK(rig.currentDistance() >= rig.settings().minDistance);
    w.setPosition(wall, {0.0f, 1.6f, 2.0f});
    rig.update();
    CHECK(rig.currentDistance() <= 1.0f - 0.25f + 1e-3f || rig.currentDistance() == rig.settings().minDistance);
    CHECK(glm::dot(observationViewFromCamera(w.camera(), 1.0f).forward, fpFwd) > 0.9999f);
    w.destroy(wall);
    rig.update();
    CHECK(std::abs(rig.currentDistance() - 3.5f) < 1e-4f);

    rig.look(0.0f, 500.0f);
    CHECK(w.camera().getPitch() <= rig.settings().maxPitch + 1e-4f);

    World free;
    free.camera().setPos({1.0f, 2.0f, 3.0f});
    free.camera().setOrientation(-90.0f, 0.0f);
    CameraRig freeRig(free);
    freeRig.update();
    CHECK(glm::length(free.camera().getPosition() - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-5f);
    freeRig.toggle();
    CHECK(glm::length(free.camera().getPosition() - glm::vec3(1.0f, 2.0f, 6.5f)) < 1e-4f);
    freeRig.update();
    CHECK(glm::length(freeRig.pivot() - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-4f);
    freeRig.toggle();
    CHECK(glm::length(free.camera().getPosition() - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-4f);
}

}

void runCameraRigTests() {
    testCameraRig();
}
