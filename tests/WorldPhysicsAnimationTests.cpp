#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "TestSupport.hpp"

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.History;
import Engine.Animation;
import Engine.Jobs;
import Engine.Profiler;
import Engine.LineEditor;
import Engine.GizmoMath;
import Engine.Selection;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

#include "WorldFixtures.hpp"
#include "WorldSuites.hpp"

namespace {

void testPhysics() {
    World world;
    PhysicsSettings settings;
    auto ground = world.create("ground", 0);
    world.add<PlaneCollider>(ground);
    auto ball = world.create("ball", 1, glm::vec3(0.0f, 5.0f, 0.0f));
    world.add<SphereCollider>(ball, 0.5f);
    world.add<RigidBody>(ball);
    int collisions = 0;
    world.events().subscribe<Collision>([&](const Collision&) { ++collisions; });

    for (int i = 0; i < 240; ++i) stepPhysics(world, 1.0f / 60.0f, settings);
    world.events().dispatch();
    CHECK(std::abs(world.getPosition(ball).y - 0.5f) < 0.01f);
    CHECK(std::abs(world.get<RigidBody>(ball)->velocity.y) < 0.5f);
    CHECK(collisions > 0);

    World bounce;
    auto floor2 = bounce.create("floor", 0);
    bounce.add<PlaneCollider>(floor2, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
    auto b = bounce.create("b", 1, glm::vec3(0.0f, 3.0f, 0.0f));
    bounce.add<SphereCollider>(b, 0.5f);
    bounce.add<RigidBody>(b, glm::vec3(0.0f), 1.0f, 1.0f, 1.0f, false);
    float maxAfterBounce = 0.0f;
    bool hit = false;
    for (int i = 0; i < 300; ++i) {
        stepPhysics(bounce, 1.0f / 60.0f, settings);
        if (bounce.get<RigidBody>(b)->velocity.y > 0.0f) hit = true;
        if (hit) maxAfterBounce = std::max(maxAfterBounce, bounce.getPosition(b).y);
    }
    CHECK(hit);
    CHECK(maxAfterBounce > 2.0f);

    World pair;
    PhysicsSettings zeroG;
    zeroG.gravity = glm::vec3(0.0f);
    auto left = pair.create("left", 1, glm::vec3(-3.0f, 0.0f, 0.0f));
    auto right = pair.create("right", 1, glm::vec3(3.0f, 0.0f, 0.0f));
    pair.add<SphereCollider>(left, 1.0f);
    pair.add<SphereCollider>(right, 1.0f);
    pair.add<RigidBody>(left, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f, 0.0f, 1.0f, false);
    pair.add<RigidBody>(right, glm::vec3(-2.0f, 0.0f, 0.0f), 1.0f, 0.0f, 1.0f, false);
    for (int i = 0; i < 240; ++i) stepPhysics(pair, 1.0f / 60.0f, zeroG);
    CHECK(pair.get<RigidBody>(left)->velocity.x < 0.0f);
    CHECK(pair.get<RigidBody>(right)->velocity.x > 0.0f);
    CHECK(pair.getPosition(left).x < pair.getPosition(right).x);

    World wall;
    auto blocker = wall.create("blocker", 1, glm::vec3(5.0f, 0.0f, 0.0f));
    wall.add<SphereCollider>(blocker, 1.0f);
    auto runner = wall.create("runner", 1, glm::vec3(0.0f, 0.0f, 0.0f));
    wall.add<SphereCollider>(runner, 1.0f);
    wall.add<RigidBody>(runner, glm::vec3(4.0f, 0.0f, 0.0f), 1.0f, 0.0f, 0.0f, false);
    for (int i = 0; i < 240; ++i) stepPhysics(wall, 1.0f / 60.0f, zeroG);
    CHECK(wall.getPosition(blocker).x == 5.0f);
    CHECK(wall.getPosition(runner).x <= 3.0f + 0.01f);
    CHECK(wall.get<RigidBody>(runner)->velocity.x < 0.5f);

    World saved;
    registerPhysicsComponents(saved);
    auto s = saved.create("s", 1);
    saved.add<RigidBody>(s, glm::vec3(1.0f, 2.0f, 3.0f), 4.0f, 0.5f, 0.25f, true);
    saved.add<SphereCollider>(s, 2.0f);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_physics_test.litscene";
    CHECK(saved.saveScene(path));
    saved.clear();
    CHECK(saved.loadScene(path));
    auto rs = saved.find("s");
    CHECK(saved.get<RigidBody>(rs) && saved.get<RigidBody>(rs)->mass == 4.0f && saved.get<RigidBody>(rs)->isStatic);
    CHECK(saved.get<SphereCollider>(rs) && saved.get<SphereCollider>(rs)->radius == 2.0f);
    std::filesystem::remove(path);
}

void testPhysicsShapesAndSleep() {
    PhysicsSettings settings;
    const float dt = 1.0f / 60.0f;

    World boxOnPlane;
    auto ground = boxOnPlane.create("ground");
    boxOnPlane.add<PlaneCollider>(ground);
    auto box = boxOnPlane.create("box", 1, glm::vec3(0.0f, 3.0f, 0.0f));
    boxOnPlane.add<BoxCollider>(box, glm::vec3(0.5f, 1.0f, 0.5f));
    boxOnPlane.add<RigidBody>(box);
    for (int i = 0; i < 240; ++i) stepPhysics(boxOnPlane, dt, settings);
    CHECK(std::abs(boxOnPlane.getPosition(box).y - 1.0f) < 0.02f);
    CHECK(boxOnPlane.get<RigidBody>(box)->sleeping);

    World stack;
    auto floor2 = stack.create("floor");
    stack.add<PlaneCollider>(floor2);
    auto lower = stack.create("lower", 1, glm::vec3(0.0f, 0.6f, 0.0f));
    auto upper = stack.create("upper", 1, glm::vec3(0.05f, 3.0f, 0.0f));
    stack.add<BoxCollider>(lower);
    stack.add<RigidBody>(lower);
    stack.add<BoxCollider>(upper);
    stack.add<RigidBody>(upper);
    for (int i = 0; i < 420; ++i) stepPhysics(stack, dt, settings);
    CHECK(std::abs(stack.getPosition(lower).y - 0.5f) < 0.05f);
    CHECK(std::abs(stack.getPosition(upper).y - 1.5f) < 0.08f);
    CHECK(stack.get<RigidBody>(upper)->sleeping && stack.get<RigidBody>(lower)->sleeping);

    World sphereOnBox;
    auto slab = sphereOnBox.create("slab", 1, glm::vec3(0.0f, -0.5f, 0.0f));
    sphereOnBox.add<BoxCollider>(slab, glm::vec3(5.0f, 0.5f, 5.0f));
    auto ball = sphereOnBox.create("ball", 1, glm::vec3(1.0f, 4.0f, 0.0f));
    sphereOnBox.add<SphereCollider>(ball, 0.5f);
    sphereOnBox.add<RigidBody>(ball);
    for (int i = 0; i < 240; ++i) stepPhysics(sphereOnBox, dt, settings);
    CHECK(std::abs(sphereOnBox.getPosition(ball).y - 0.5f) < 0.02f);

    World masks;
    PhysicsSettings zeroG;
    zeroG.gravity = glm::vec3(0.0f);
    zeroG.setLayersCollide(1, 2, false);
    auto la = masks.create("la", 1, glm::vec3(-2.0f, 0.0f, 0.0f));
    auto lb = masks.create("lb", 1, glm::vec3(2.0f, 0.0f, 0.0f));
    masks.setLayer(la, 1u << 1);
    masks.setLayer(lb, 1u << 2);
    masks.add<SphereCollider>(la, 1.0f);
    masks.add<SphereCollider>(lb, 1.0f);
    masks.add<RigidBody>(la, glm::vec3(3.0f, 0.0f, 0.0f), 1.0f, 0.0f, 0.0f, false);
    masks.add<RigidBody>(lb, glm::vec3(-3.0f, 0.0f, 0.0f), 1.0f, 0.0f, 0.0f, false);
    for (int i = 0; i < 90; ++i) stepPhysics(masks, dt, zeroG);
    CHECK(masks.get<RigidBody>(la)->velocity.x == 3.0f);
    CHECK(masks.getPosition(la).x > masks.getPosition(lb).x);

    World rays;
    auto sphere = rays.create("sphere", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    rays.add<SphereCollider>(sphere, 1.0f);
    auto slab2 = rays.create("box", 1, glm::vec3(5.0f, 0.0f, -20.0f));
    rays.add<BoxCollider>(slab2, glm::vec3(1.0f));
    auto plane = rays.create("plane", NO_MESH, glm::vec3(0.0f, -5.0f, 0.0f));
    rays.add<PlaneCollider>(plane);
    auto hit = physicsRaycast(rays, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == sphere && std::abs(hit->distance - 9.0f) < 1.0e-4f);
    CHECK(hit && nearlyEqual(hit->normal, glm::vec3(0.0f, 0.0f, 1.0f)));
    hit = physicsRaycast(rays, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == slab2 && std::abs(hit->distance - 19.0f) < 1.0e-4f);
    CHECK(hit && nearlyEqual(hit->normal, glm::vec3(0.0f, 0.0f, 1.0f)));
    hit = physicsRaycast(rays, glm::vec3(20.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(hit && hit->entity == plane && std::abs(hit->distance - 5.0f) < 1.0e-4f);
    CHECK(!physicsRaycast(rays, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 5.0f));
    CHECK(!physicsRaycast(rays, glm::vec3(20.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));

    World wake;
    auto floor3 = wake.create("floor");
    wake.add<PlaneCollider>(floor3);
    auto sleeper = wake.create("sleeper", 1, glm::vec3(0.0f, 0.5f, 0.0f));
    wake.add<SphereCollider>(sleeper, 0.5f);
    wake.add<RigidBody>(sleeper);
    for (int i = 0; i < 120; ++i) stepPhysics(wake, dt, settings);
    CHECK(wake.get<RigidBody>(sleeper)->sleeping);
    const float restingY = wake.getPosition(sleeper).y;
    auto hitter = wake.create("hitter", 1, glm::vec3(3.0f, 0.5f, 0.0f));
    wake.add<SphereCollider>(hitter, 0.5f);
    wake.add<RigidBody>(hitter, glm::vec3(-6.0f, 0.0f, 0.0f), 1.0f, 1.0f, 0.0f, false);
    bool woke = false;
    for (int i = 0; i < 60; ++i) {
        stepPhysics(wake, dt, settings);
        if (!wake.get<RigidBody>(sleeper)->sleeping) woke = true;
    }
    CHECK(woke);
    CHECK(wake.getPosition(sleeper).x < -0.1f);
    CHECK(std::abs(wake.getPosition(sleeper).y - restingY) < 0.05f);
    wakeBody(wake, sleeper);
    CHECK(!wake.get<RigidBody>(sleeper)->sleeping);

    World saved;
    registerPhysicsComponents(saved);
    auto s = saved.create("s", 1);
    saved.add<BoxCollider>(s, glm::vec3(1.0f, 2.0f, 3.0f));
    saved.add<RigidBody>(s, glm::vec3(0.0f), 1.0f, 1.0f, 0.0f, false);
    saved.get<RigidBody>(s)->sleeping = true;
    const auto path = std::filesystem::temp_directory_path() / "lit_world_box_test.litscene";
    CHECK(saved.saveScene(path));
    saved.clear();
    CHECK(saved.loadScene(path));
    auto rs = saved.find("s");
    CHECK(saved.get<BoxCollider>(rs) && saved.get<BoxCollider>(rs)->halfExtents.z == 3.0f);
    CHECK(saved.get<RigidBody>(rs) && saved.get<RigidBody>(rs)->sleeping);
    std::filesystem::remove(path);
}

void testAnimation() {
    Engine engine;
    World& world = engine.world();
    const auto step = [&](float seconds) {
        while (seconds > 0.0f) {
            const float chunk = std::min(seconds, 0.125f);
            engine.tick(chunk);
            seconds -= chunk;
        }
    };
    AnimationClip clip;
    clip.name = "slide";
    clip.position = {{2.0f, glm::vec3(10.0f, 10.0f, 0.0f)}, {0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(10.0f, 0.0f, 0.0f)}};
    clip.rotation = {{0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)}, {2.0f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f))}};
    clip.scale = {{0.0f, glm::vec3(1.0f)}, {2.0f, glm::vec3(3.0f)}};
    const uint32_t id = engine.animations().add(clip);
    CHECK(engine.animations().find("slide") == id);
    CHECK(engine.animations().find("nope") == INVALID_ENTITY);
    CHECK(engine.animations().get(id)->duration() == 2.0f);

    auto e = world.create("e", 1);
    world.add<Animator>(e, Animator{id, 0.0f, 1.0f, true});
    step(0.5f);
    CHECK(nearlyEqual(world.getPosition(e), glm::vec3(5.0f, 0.0f, 0.0f)));
    CHECK(nearlyEqual(world.getScale(e), glm::vec3(1.5f), 1.0e-3f));
    CHECK(nearlyEqual(world.forward(e), glm::vec3(std::sin(glm::radians(-22.5f)), 0.0f, -std::cos(glm::radians(22.5f))), 1.0e-3f));

    step(1.0f);
    CHECK(nearlyEqual(world.getPosition(e), glm::vec3(10.0f, 5.0f, 0.0f)));
    step(0.25f);
    CHECK(nearlyEqual(world.getPosition(e), glm::vec3(10.0f, 7.5f, 0.0f)) || world.get<Animator>(e)->time < 2.0f);
    step(0.25f);
    CHECK(world.get<Animator>(e)->time >= 0.0f && world.get<Animator>(e)->time < 0.1f);
    CHECK(nearlyEqual(world.getPosition(e), glm::vec3(0.0f), 0.5f));

    world.get<Animator>(e)->speed = 2.0f;
    world.get<Animator>(e)->time = 0.0f;
    step(0.25f);
    CHECK(nearlyEqual(world.getPosition(e), glm::vec3(10.0f, 0.0f, 0.0f)) == false);
    CHECK(std::abs(world.get<Animator>(e)->time - 0.5f) < 1.0e-4f);

    AnimationClip once;
    once.name = "once";
    once.loop = false;
    once.position = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(0.0f, 4.0f, 0.0f)}};
    const uint32_t onceId = engine.animations().add(once);
    auto f = world.create("f", 1, glm::vec3(0.0f, 0.0f, 9.0f));
    world.add<Animator>(f, Animator{onceId, 0.0f, 1.0f, true});
    step(0.25f);
    step(0.25f);
    step(0.25f);
    step(0.25f);
    step(0.25f);
    CHECK(nearlyEqual(world.getPosition(f), glm::vec3(0.0f, 4.0f, 0.0f)));
    CHECK(!world.get<Animator>(f)->playing);
    step(0.25f);
    CHECK(nearlyEqual(world.getPosition(f), glm::vec3(0.0f, 4.0f, 0.0f)));

    world.get<Animator>(f)->time = 0.0f;
    world.get<Animator>(f)->playing = true;
    engine.setSystemEnabled("animation", false);
    step(0.5f);
    CHECK(nearlyEqual(world.getPosition(f), glm::vec3(0.0f, 4.0f, 0.0f)));
    engine.setSystemEnabled("animation", true);

    AnimationClip partial;
    partial.name = "scale-only";
    partial.scale = {{0.0f, glm::vec3(1.0f)}, {1.0f, glm::vec3(2.0f)}};
    const uint32_t partialId = engine.animations().add(partial);
    auto g = world.create("g", 1, glm::vec3(7.0f, 8.0f, 9.0f));
    world.add<Animator>(g, Animator{partialId, 0.0f, 1.0f, true});
    step(0.5f);
    CHECK(nearlyEqual(world.getPosition(g), glm::vec3(7.0f, 8.0f, 9.0f)));
    CHECK(nearlyEqual(world.getScale(g), glm::vec3(1.5f), 1.0e-3f));

    registerAnimationComponents(world);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_anim_test.litscene";
    world.get<Animator>(g)->time = 0.25f;
    world.get<Animator>(g)->speed = 3.0f;
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    auto rg = world.find("g");
    CHECK(world.get<Animator>(rg) && world.get<Animator>(rg)->clip == partialId && world.get<Animator>(rg)->speed == 3.0f && world.get<Animator>(rg)->time == 0.25f);
    std::filesystem::remove(path);
}

void testAnimationAgreement() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 0.5f); });
    world.create("root", 0);
    std::vector<EntityHandle> movers;
    std::vector<glm::vec3> base;
    for (int i = 0; i < 4; ++i) {
        base.push_back(glm::vec3(float(i) * 10.0f, 0.0f, -20.0f));
        movers.push_back(world.create("m", 1, base.back()));
    }
    auto follower = world.create("follower", 1, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), movers[2]);
    world.setAnimation(movers[0].index, base);
    world.setAnimationTime(1.25f);
    for (uint32_t i = 0; i < 4; ++i) {
        const glm::vec3 expected = base[i] + World::orbitOffset(1.25f, i);
        const glm::vec3 actual = world.getWorldPosition(movers[i]);
        CHECK(glm::length(actual - expected) < 1.0e-4f);
    }
    CHECK(glm::length(world.getWorldPosition(follower) - (base[2] + World::orbitOffset(1.25f, 2) + glm::vec3(0.0f, 3.0f, 0.0f))) < 1.0e-4f);

    const glm::vec3 target = base[1] + World::orbitOffset(1.25f, 1);
    auto hit = world.raycast(target + glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == movers[1]);

    world.setAnimationTime(4.0f);
    const glm::vec3 moved = base[1] + World::orbitOffset(4.0f, 1);
    CHECK(glm::length(world.getWorldPosition(movers[1]) - moved) < 1.0e-4f);
    hit = world.raycast(moved + glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == movers[1]);
}

}

void runPhysicsAnimationTests() {
    testPhysics();
    testPhysicsShapesAndSleep();
    testAnimation();
    testAnimationAgreement();
}
