#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.Portal;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.Character;
import Engine.CameraRig;
import Engine.glm;

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    }
}
}  // namespace

#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

constexpr uint32_t kSphere = 2;

static const char* kWorld = R"({
  "cells": [
    {"name": "X", "min": [-30, -1, -30], "max": [30, 8, 30]},
    {"name": "Y", "min": [70, -1, -30], "max": [130, 8, 30]}
  ],
  "portals": [
    {"name": "turn", "a": {"cell": "X", "position": [10, 1.5, 0], "rotation": [-90, 0, 0], "size": [1.5, 1.5]},
                     "b": {"cell": "Y", "position": [100, 1.5, 0], "rotation": [0, 0, 0], "size": [1.5, 1.5]}}
  ]
})";

static const char* kBlend = R"({
  "cells": [
    {"name": "X", "min": [-30, -1, -30], "max": [30, 8, 30], "gravity": [0, -9.81, 0]},
    {"name": "Y", "min": [70, -1, -30], "max": [130, 8, 30], "gravity": [0, -2, 0]}
  ],
  "portals": [
    {"name": "turn", "a": {"cell": "X", "position": [10, 1.5, 0], "rotation": [-90, 0, 0], "size": [1.5, 1.5]},
                     "b": {"cell": "Y", "position": [100, 1.5, 0], "rotation": [0, 0, 0], "size": [1.5, 1.5]}}
  ]
})";

static const char* kField = R"({
  "cells": [
    {"name": "Z", "min": [-50, -50, -50], "max": [50, 50, 50], "gravity": [3, -9, 0]}
  ],
  "portals": []
})";

static void setup(World& w) {
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
}

static EntityHandle addPlane(World& w, const glm::vec3& normal, const glm::vec3& at = glm::vec3(0.0f)) {
    EntityDesc d;
    d.position = at;
    const EntityHandle e = w.create(d);
    w.add<PlaneCollider>(e, normal);
    return e;
}

static EntityHandle addBox(World& w, const glm::vec3& center, const glm::vec3& half) {
    EntityDesc d;
    d.position = center;
    const EntityHandle e = w.create(d);
    w.add<BoxCollider>(e, half);
    return e;
}

static float yawOf(const glm::vec3& f) { return glm::degrees(std::atan2(f.z, f.x)); }
static glm::vec3 rot(const glm::mat4& m, const glm::vec3& v) { return glm::vec3(m * glm::vec4(v, 0.0f)); }
static glm::vec3 pos(const glm::mat4& m, const glm::vec3& v) { return glm::vec3(m * glm::vec4(v, 1.0f)); }

static int portalIn(const PortalSystem& p, const char* cell) {
    const int c = p.findCell(cell);
    for (size_t i = 0; i < p.portals().size(); ++i) {
        if (p.portals()[i].cell == c) return static_cast<int>(i);
    }
    return -1;
}

static void crossing(float angleDeg, float speed, CameraMode mode) {
    World w;
    setup(w);
    PortalSystem p;
    CHECK(p.parseJson(kWorld));
    installPortalPhysics(p);
    addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
    PhysicsSettings ps;
    CharacterSettings cs;
    cs.speed = speed;
    CharacterController c(w, p, ps, cs);
    c.rig().setMode(mode);
    const int idA = portalIn(p, "X");
    const Portal& A = p.portals()[static_cast<size_t>(idA)];
    const glm::vec3 n = A.normal();
    const glm::vec3 target(A.center().x, 0.0f, A.center().z);
    const float a = glm::radians(angleDeg);
    const glm::vec3 start = target + n * (7.0f * std::cos(a)) + A.right() * (7.0f * std::sin(a));
    const glm::vec3 dir = glm::normalize(target - start);
    c.spawn(start, yawOf(dir));
    const float dt = 1.0f / 60.0f;
    glm::vec3 prev = w.camera().getPosition();
    int prevCell = p.cameraCell();
    float worst = 0.0f;
    int after = -1;
    glm::vec3 speedAfter(0.0f);
    for (int i = 0; i < 1200; ++i) {
        CharacterInput in;
        in.move = glm::vec2(0.0f, 1.0f);
        c.setInput(in);
        c.update(dt);
        const glm::vec3 cam = w.camera().getPosition();
        const int cellNow = p.cameraCell();
        glm::vec3 from = prev;
        if (cellNow != prevCell) {
            for (const Portal& q : p.portals()) {
                if (q.cell == prevCell && p.portals()[static_cast<size_t>(q.partner)].cell == cellNow) from = pos(q.transform, prev);
            }
        }
        worst = std::max(worst, glm::length(cam - from));
        prev = cam;
        prevCell = cellNow;
        if (c.crossings() > 0 && after < 0) after = static_cast<int>(6.0f / speed / dt) + 10;
        if (after >= 0 && --after == 0) break;
    }
    CHECK(c.crossings() == 1);
    CHECK(c.cell() == p.findCell("Y"));
    CHECK(p.cameraCell() == p.findCell("Y"));
    CHECK(worst < speed * dt * 1.5f + 0.02f);
    const glm::vec3 expected = rot(A.transform, dir);
    const glm::vec3 v = c.velocity();
    CHECK(glm::length(v) > speed * 0.9f && glm::length(v) < speed * 1.1f);
    CHECK(glm::dot(glm::normalize(v), expected) > 0.98f);
    CHECK(c.grounded());
}

static void testCrossings() {
    for (CameraMode mode : {CameraMode::FirstPerson, CameraMode::ThirdPerson}) {
        for (float angle : {0.0f, 25.0f, -40.0f, 60.0f}) {
            for (float speed : {2.0f, 4.0f, 9.0f}) crossing(angle, speed, mode);
        }
    }
}

static void testThrown() {
    World w;
    setup(w);
    PortalSystem p;
    CHECK(p.parseJson(kWorld));
    installPortalPhysics(p);
    addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
    const Portal& A = p.portals()[static_cast<size_t>(portalIn(p, "X"))];
    const Portal& B = p.portals()[static_cast<size_t>(A.partner)];
    const glm::vec3 n = A.normal();
    const glm::vec3 exitDir = rot(A.transform, -n);
    const glm::vec3 wallCenter = B.center() + exitDir * 6.0f;
    addBox(w, glm::vec3(wallCenter.x, 2.0f, wallCenter.z), glm::vec3(std::abs(exitDir.x) > 0.5f ? 0.5f : 6.0f, 6.0f, std::abs(exitDir.z) > 0.5f ? 0.5f : 6.0f));
    EntityDesc d;
    d.mesh = kSphere;
    d.scale = glm::vec3(0.3f);
    d.position = glm::vec3(A.center().x, 2.0f, A.center().z) + n * 5.0f;
    const EntityHandle ball = w.create(d);
    w.add<SphereCollider>(ball, 0.3f);
    RigidBody* rb = w.add<RigidBody>(ball);
    rb->velocity = -n * 12.0f;
    p.track(w, ball);
    PhysicsSettings ps;
    bool twin = false;
    bool crossed = false;
    glm::vec3 before(0.0f);
    for (int i = 0; i < 600; ++i) {
        const glm::vec3 vBefore = w.get<RigidBody>(ball)->velocity;
        const int cellBefore = p.objectCell(ball);
        stepPortalPhysics(p, w, 1.0f / 120.0f, ps);
        const EntityHandle g = p.ghostOf(ball);
        if (w.isAlive(g) && w.isVisible(g)) twin = true;
        if (!crossed && p.objectCell(ball) != cellBefore) {
            crossed = true;
            before = vBefore;
            const glm::vec3 vAfter = w.get<RigidBody>(ball)->velocity;
            CHECK(glm::length(vAfter - rot(A.transform, vBefore)) < 0.3f);
        }
    }
    CHECK(twin);
    CHECK(crossed);
    CHECK(p.objectCell(ball) == p.findCell("Y"));
    const glm::vec3 end = w.getWorldPosition(ball);
    CHECK(glm::dot(end - B.center(), exitDir) > 3.0f);
    CHECK(glm::dot(end - B.center(), exitDir) < 5.3f);
    CHECK(end.y < 0.5f);
    CHECK(glm::length(w.get<RigidBody>(ball)->velocity) < 1.0f);
}

static void testSweep() {
    World w;
    setup(w);
    PortalSystem p;
    CHECK(p.parseJson(kWorld));
    const Portal& A = p.portals()[static_cast<size_t>(portalIn(p, "X"))];
    const Portal& B = p.portals()[static_cast<size_t>(A.partner)];
    const glm::vec3 n = A.normal();
    const glm::vec3 exitDir = rot(A.transform, -n);
    const glm::vec3 wallCenter = B.center() + exitDir * 8.0f;
    const EntityHandle wall = addBox(w, wallCenter, glm::vec3(std::abs(exitDir.x) > 0.5f ? 0.5f : 5.0f, 5.0f, std::abs(exitDir.z) > 0.5f ? 0.5f : 5.0f));
    const glm::vec3 origin = A.center() + n * 5.0f;
    const PortalCast sweep = portalSweep(p, w, origin, -n, 40.0f, 0.2f);
    CHECK(sweep.hit && sweep.hit->entity == wall);
    CHECK(sweep.crossings == 1);
    CHECK(sweep.cell == p.findCell("Y"));
    CHECK(std::abs(sweep.distance - 12.3f) < 0.02f);
    CHECK(glm::length(sweep.direction - exitDir) < 1.0e-3f);
    const PortalCast ray = portalRaycast(p, w, origin, -n, 40.0f);
    CHECK(ray.hit && ray.hit->entity == wall);
    CHECK(std::abs(ray.distance - 12.5f) < 0.02f);
    const PortalCast miss = portalRaycast(p, w, origin, n, 40.0f);
    CHECK(!miss.hit && miss.crossings == 0);

    const glm::vec3 near = B.center() - B.normal() * 0.2f;
    const EntityHandle ball = addBox(w, near, glm::vec3(0.1f));
    const auto overlaps = portalOverlap(p, w, A.center() + n * 0.2f, 0.5f);
    bool found = false;
    for (const PortalOverlap& o : overlaps) found = found || (o.entity == ball && o.crossings == 1);
    CHECK(found);
    const auto none = portalOverlap(p, w, A.center() + n * 3.0f, 0.5f);
    CHECK(none.empty());
}

static void testStepsAndSlopes() {
    {
        World w;
        setup(w);
        PortalSystem p;
        PhysicsSettings ps;
        addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
        addBox(w, glm::vec3(3.0f, 0.15f, 0.0f), glm::vec3(1.0f, 0.15f, 3.0f));
        CharacterController c(w, p, ps);
        c.spawn(glm::vec3(0.0f), 0.0f);
        for (int i = 0; i < 240; ++i) {
            c.setInput({glm::vec2(0.0f, 1.0f), false, false});
            c.update(1.0f / 60.0f);
        }
        CHECK(c.position().x > 3.0f);
        CHECK(std::abs(c.position().y - 0.3f) < 0.02f);
        CHECK(c.grounded());
    }
    {
        World w;
        setup(w);
        PortalSystem p;
        PhysicsSettings ps;
        addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
        addBox(w, glm::vec3(3.0f, 0.5f, 0.0f), glm::vec3(1.0f, 0.5f, 3.0f));
        CharacterController c(w, p, ps);
        c.spawn(glm::vec3(0.0f), 0.0f);
        for (int i = 0; i < 240; ++i) {
            c.setInput({glm::vec2(0.0f, 1.0f), false, false});
            c.update(1.0f / 60.0f);
        }
        CHECK(c.position().x < 2.0f);
        CHECK(c.position().y < 0.05f);
    }
    for (float degrees : {30.0f, 65.0f}) {
        World w;
        setup(w);
        PortalSystem p;
        PhysicsSettings ps;
        const float a = glm::radians(degrees);
        addPlane(w, glm::vec3(-std::sin(a), std::cos(a), 0.0f));
        CharacterController c(w, p, ps);
        c.spawn(glm::vec3(0.0f), 0.0f);
        for (int i = 0; i < 180; ++i) {
            c.setInput({glm::vec2(0.0f, 1.0f), false, false});
            c.update(1.0f / 60.0f);
        }
        if (degrees < 45.0f) {
            CHECK(c.grounded());
            CHECK(c.position().x > 2.0f);
            CHECK(std::abs(c.position().y - c.position().x * std::tan(a)) < 0.05f);
        } else {
            CHECK(c.position().x < 1.5f);
        }
    }
    {
        World w;
        setup(w);
        PortalSystem p;
        PhysicsSettings ps;
        addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
        CharacterController c(w, p, ps);
        c.spawn(glm::vec3(0.0f), 0.0f);
        c.update(0.5f);
        c.setInput({glm::vec2(0.0f), true, false});
        c.update(1.0f / 60.0f);
        CHECK(!c.grounded());
        float peak = 0.0f;
        for (int i = 0; i < 120; ++i) {
            c.update(1.0f / 60.0f);
            peak = std::max(peak, c.position().y);
        }
        CHECK(peak > 0.8f && peak < 1.3f);
        CHECK(c.grounded());
    }
}

static void testGravityFields() {
    {
        World w;
        setup(w);
        PortalSystem p;
        CHECK(p.parseJson(kField));
        PhysicsSettings ps;
        CharacterController c(w, p, ps);
        c.spawn(glm::vec3(0.0f), 0.0f);
        for (int i = 0; i < 120; ++i) c.update(1.0f / 60.0f);
        const glm::vec3 target = -glm::normalize(glm::vec3(3.0f, -9.0f, 0.0f));
        CHECK(glm::dot(c.up(), target) > 0.995f);
        CHECK(glm::dot(glm::normalize(c.velocity()), -target) > 0.95f);
    }
    {
        World w;
        setup(w);
        PortalSystem p;
        CHECK(p.parseJson(kBlend));
        PhysicsSettings ps;
        CharacterController c(w, p, ps);
        const Portal& A = p.portals()[static_cast<size_t>(portalIn(p, "X"))];
        const glm::vec3 n = A.normal();
        c.spawn(glm::vec3(A.center().x, 0.0f, A.center().z) + n * 0.1f, 0.0f);
        const float nearG = glm::length(c.gravity());
        c.spawn(glm::vec3(A.center().x, 0.0f, A.center().z) + n * 8.0f, 0.0f);
        const float farG = glm::length(c.gravity());
        CHECK(std::abs(farG - 9.81f) < 0.01f);
        CHECK(nearG < 8.0f && nearG > 3.5f);
    }
}

static void testCameraRig() {
    World w;
    setup(w);
    PortalSystem p;
    PhysicsSettings ps;
    addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
    addBox(w, glm::vec3(-3.0f, 2.0f, 0.0f), glm::vec3(0.5f, 3.0f, 5.0f));
    CharacterController c(w, p, ps);
    c.spawn(glm::vec3(0.0f), 0.0f);
    c.update(0.1f);
    const glm::vec3 before = c.position();
    const glm::vec3 eyePos = w.camera().getPosition();
    c.rig().toggle();
    c.update(1.0e-4f);
    CHECK(glm::length(c.position() - before) < 1.0e-3f);
    CHECK(c.rig().mode() == CameraMode::ThirdPerson);
    const float dist = glm::length(w.camera().getPosition() - c.eye());
    CHECK(dist < 2.5f);
    CHECK(w.camera().getPosition().x > -2.5f);
    c.rig().toggle();
    c.update(1.0e-4f);
    CHECK(glm::length(w.camera().getPosition() - eyePos) < 0.05f);
}

static void testImpact() {
    World w;
    setup(w);
    PortalSystem p;
    PhysicsSettings ps;
    addPlane(w, glm::vec3(0.0f, 1.0f, 0.0f));
    CharacterController c(w, p, ps);
    c.spawn(glm::vec3(0.0f), 0.0f);
    c.update(0.2f);
    EntityDesc d;
    d.position = glm::vec3(4.0f, 1.0f, 0.0f);
    const EntityHandle mug = w.create(d);
    w.add<SphereCollider>(mug, 0.1f);
    RigidBody* rb = w.add<RigidBody>(mug);
    rb->velocity = glm::vec3(-8.0f, 0.0f, 0.0f);
    rb->gravityScale = 0.0f;
    rb->mass = 0.4f;
    int hits = 0;
    float speed = 0.0f;
    c.setImpactHook([&](const CharacterImpact& i) {
        ++hits;
        speed = i.speed;
    });
    float peakKick = 0.0f;
    for (int i = 0; i < 180; ++i) {
        stepPhysics(w, 1.0f / 60.0f, ps);
        w.events().dispatch();
        c.update(1.0f / 60.0f);
        peakKick = std::max(peakKick, c.kick().y);
    }
    CHECK(hits == 1);
    CHECK(speed > 5.0f);
    CHECK(peakKick > 0.5f);
    CHECK(std::abs(c.kick().y) < 0.05f);
    CHECK(c.position().x <= 0.1f && c.position().x > -2.0f);
    CHECK(c.grounded());
    c.setInput({glm::vec2(0.0f, 1.0f), false, false});
    for (int i = 0; i < 60; ++i) c.update(1.0f / 60.0f);
    CHECK(c.position().x > 1.0f);
}

int main() {
    testSweep();
    testThrown();
    testCrossings();
    testStepsAndSlopes();
    testGravityFields();
    testCameraRig();
    testImpact();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
