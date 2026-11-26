#include "TestSupport.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.Portal;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.glm;

namespace {

void setup(World& w) {
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
}

EntityHandle makeEntity(World& w, const glm::vec3& at, const glm::vec3& scale = glm::vec3(1.0f)) {
    EntityDesc d;
    d.position = at;
    d.scale = scale;
    return w.create(d);
}

EntityHandle addPlane(World& w, const glm::vec3& normal, const glm::vec3& at = glm::vec3(0.0f)) {
    const EntityHandle e = makeEntity(w, at);
    w.add<PlaneCollider>(e, normal);
    return e;
}

EntityHandle addBox(World& w, const glm::vec3& at, const glm::vec3& half, bool dynamic = false) {
    const EntityHandle e = makeEntity(w, at);
    w.add<BoxCollider>(e, half);
    if (dynamic) w.add<RigidBody>(e);
    return e;
}

EntityHandle addSphere(World& w, const glm::vec3& at, float radius, bool dynamic = true) {
    const EntityHandle e = makeEntity(w, at);
    w.add<SphereCollider>(e, radius);
    if (dynamic) w.add<RigidBody>(e);
    return e;
}

std::shared_ptr<const TriangleMeshData> makeFloor(int n, float half, float y = 0.0f) {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) vertices.emplace_back(-half + 2.0f * half * static_cast<float>(i) / static_cast<float>(n), y, -half + 2.0f * half * static_cast<float>(j) / static_cast<float>(n));
    }
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * (n + 1) + i);
            const uint32_t b = a + 1;
            const uint32_t c = a + static_cast<uint32_t>(n + 1);
            const uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
    return makeTriangleMesh(vertices, indices);
}

std::shared_ptr<const TriangleMeshData> makeWall(float half, float height) {
    return makeTriangleMesh({{0, 0, -half}, {0, height, -half}, {0, height, half}, {0, 0, half}}, {0, 1, 2, 0, 2, 3});
}

EntityHandle addMesh(World& w, std::shared_ptr<const TriangleMeshData> mesh, const glm::vec3& at = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1, 0, 0, 0)) {
    const EntityHandle e = makeEntity(w, at, scale);
    w.setRotation(e, rotation);
    w.add<TriMeshCollider>(e, mesh);
    return e;
}

std::vector<glm::vec3> cubePoints(float h) {
    std::vector<glm::vec3> p;
    for (int i = 0; i < 8; ++i) p.emplace_back((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
    return p;
}

void run(World& w, int steps, float dt = 1.0f / 120.0f, const PhysicsSettings& settings = {}) {
    for (int i = 0; i < steps; ++i) stepPhysics(w, dt, settings);
}

bool near(float a, float b, float eps) { return std::abs(a - b) <= eps; }

void testMeshQueries() {
    World w;
    setup(w);
    const auto floor = makeFloor(16, 10.0f);
    CHECK(floor && floor->valid() && floor->triangleCount() == 512);
    const EntityHandle mesh = addMesh(w, floor);
    {
        const auto hit = physicsRaycast(w, glm::vec3(1.3f, 5.0f, -2.1f), glm::vec3(0, -1, 0), 100.0f);
        CHECK(hit && hit->entity == mesh && near(hit->distance, 5.0f, 1e-4f) && hit->normal.y > 0.99f);
        const auto up = physicsRaycast(w, glm::vec3(1.3f, -5.0f, -2.1f), glm::vec3(0, 1, 0), 100.0f);
        CHECK(up && near(up->distance, 5.0f, 1e-4f) && up->normal.y < -0.99f);
        CHECK(!physicsRaycast(w, glm::vec3(30.0f, 5.0f, 0.0f), glm::vec3(0, -1, 0), 100.0f));
        CHECK(!physicsRaycast(w, glm::vec3(1.0f, 5.0f, 0.0f), glm::vec3(0, -1, 0), 4.0f));
    }
    {
        const auto sweep = physicsSweepSphere(w, glm::vec3(0.4f, 5.0f, 0.4f), glm::vec3(0, -1, 0), 100.0f, 0.5f);
        CHECK(sweep && near(sweep->distance, 4.5f, 1e-3f) && sweep->normal.y > 0.99f);
        const auto edge = physicsSweepSphere(w, glm::vec3(10.4f, 3.0f, 0.0f), glm::vec3(0, -1, 0), 100.0f, 0.5f);
        CHECK(edge && near(edge->distance, 2.7f, 0.02f));
        const auto start = physicsSweepSphere(w, glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(1, 0, 0), 5.0f, 0.5f);
        CHECK(start && start->distance == 0.0f);
    }
    {
        const auto contacts = physicsOverlapCapsule(w, glm::vec3(0.5f, 0.3f, 0.5f), glm::vec3(0.5f, 1.1f, 0.5f), 0.5f);
        CHECK(contacts.size() >= 1 && near(contacts.front().depth, 0.2f, 1e-3f) && contacts.front().normal.y > 0.99f);
        CHECK(physicsOverlapCapsule(w, glm::vec3(0.5f, 0.7f, 0.5f), glm::vec3(0.5f, 1.5f, 0.5f), 0.5f).empty());
        const auto pierce = physicsOverlapCapsule(w, glm::vec3(0.5f, -1.0f, 0.5f), glm::vec3(0.5f, 1.0f, 0.5f), 0.25f);
        CHECK(!pierce.empty() && pierce.front().depth > 0.25f);
        const auto box = physicsOverlapBox(w, glm::vec3(0.5f, 0.4f, 0.5f), glm::vec3(0.5f));
        CHECK(!box.empty() && near(box.front().depth, 0.1f, 1e-3f) && box.front().normal.y > 0.99f);
        CHECK(physicsOverlapBox(w, glm::vec3(0.5f, 0.6f, 0.5f), glm::vec3(0.5f)).empty());
    }
    World w2;
    setup(w2);
    addMesh(w2, makeWall(10.0f, 5.0f), glm::vec3(3.0f, 0.0f, 0.0f));
    {
        const auto ray = physicsRaycast(w2, glm::vec3(-5, 1, 0), glm::vec3(1, 0, 0), 50.0f);
        CHECK(ray && near(ray->distance, 8.0f, 1e-4f) && ray->normal.x < -0.99f);
        const auto sphere = physicsSweepSphere(w2, glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), 50.0f, 0.4f);
        CHECK(sphere && near(sphere->distance, 2.6f, 1e-3f));
        const auto capsule = physicsSweepCapsule(w2, glm::vec3(0, 0.5f, 0), glm::vec3(0, 1.5f, 0), 0.3f, glm::vec3(1, 0, 0), 50.0f);
        CHECK(capsule && near(capsule->distance, 2.7f, 1e-3f));
        const auto box = physicsSweepBox(w2, glm::vec3(0, 1, 0), glm::vec3(0.4f, 0.5f, 0.5f), glm::vec3(1, 0, 0), 50.0f);
        CHECK(box && near(box->distance, 2.6f, 0.01f) && box->normal.x < -0.5f);
        CHECK(!physicsSweepBox(w2, glm::vec3(0, 1, 0), glm::vec3(0.4f), glm::vec3(0, 0, 1), 5.0f));
    }
    const auto contacts = meshSphereContacts(*floor, glm::vec3(0.0f, 0.3f, 0.0f), 0.5f);
    CHECK(!contacts.empty() && near(contacts.front().depth, 0.2f, 1e-4f));
}

void testMeshTransform() {
    World w;
    setup(w);
    const auto floor = makeFloor(8, 4.0f);
    const glm::quat roll = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    addMesh(w, floor, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(1.0f), roll);
    const auto hit = physicsRaycast(w, glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(1, 0, 0), 20.0f);
    CHECK(hit && near(hit->distance, 5.0f, 1e-3f) && hit->normal.x < -0.99f);
    CHECK(physicsOverlapCapsule(w, glm::vec3(4.8f, 1, 1), glm::vec3(4.8f, 2, 1), 0.4f).size() >= 1);
    CHECK(physicsOverlapCapsule(w, glm::vec3(4.4f, 1, 1), glm::vec3(4.4f, 2, 1), 0.4f).empty());
    World w2;
    setup(w2);
    addMesh(w2, floor, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(2.0f));
    const auto scaled = physicsRaycast(w2, glm::vec3(7.0f, 10.0f, 7.0f), glm::vec3(0, -1, 0), 50.0f);
    CHECK(scaled && near(scaled->distance, 7.0f, 1e-3f));
    CHECK(!physicsRaycast(w2, glm::vec3(9.0f, 10.0f, 0.0f), glm::vec3(0, -1, 0), 50.0f));
    const auto sweep = physicsSweepSphere(w2, glm::vec3(7.0f, 10.0f, 7.0f), glm::vec3(0, -1, 0), 50.0f, 1.0f);
    CHECK(sweep && near(sweep->distance, 6.0f, 1e-2f));
    World w3;
    setup(w3);
    addMesh(w3, floor, glm::vec3(0.0f), glm::vec3(1.0f, 5.0f, 3.0f));
    const auto stretched = physicsRaycast(w3, glm::vec3(10.0f, 4.0f, 10.0f), glm::normalize(glm::vec3(-1, -0.2f, -1)), 100.0f);
    CHECK(!stretched);
    const auto straight = physicsRaycast(w3, glm::vec3(11.0f, 4.0f, 11.0f), glm::vec3(0, -1, 0), 100.0f);
    CHECK(!straight);
    const auto inside = physicsRaycast(w3, glm::vec3(3.0f, 4.0f, 3.0f), glm::normalize(glm::vec3(-1, -1, -1)), 100.0f);
    CHECK(inside && near(inside->distance, 4.0f * std::sqrt(3.0f), 1e-2f));
}

void testDynamicOnMesh() {
    World w;
    setup(w);
    addMesh(w, makeFloor(10, 10.0f));
    const EntityHandle ball = addSphere(w, glm::vec3(1.0f, 3.0f, 1.0f), 0.5f);
    const EntityHandle box = addBox(w, glm::vec3(-3.0f, 3.0f, 1.0f), glm::vec3(0.4f, 0.6f, 0.3f), true);
    const EntityHandle capsule = makeEntity(w, glm::vec3(4.0f, 3.0f, -2.0f));
    w.add<CapsuleCollider>(capsule, 0.3f, 0.5f);
    w.add<RigidBody>(capsule);
    run(w, 480);
    CHECK(near(w.getWorldPosition(ball).y, 0.5f, 0.02f));
    CHECK(near(w.getWorldPosition(box).y, 0.6f, 0.02f));
    CHECK(near(w.getWorldPosition(capsule).y, 0.8f, 0.02f));
    CHECK(w.get<RigidBody>(box)->sleeping);
    CHECK(near(w.getWorldPosition(box).x, -3.0f, 0.05f) && near(w.getWorldPosition(box).z, 1.0f, 0.05f));
}

void testMeshSlopeFriction() {
    for (const float degrees : {20.0f, 40.0f}) {
        World w;
        setup(w);
        const glm::quat tilt = glm::angleAxis(glm::radians(-degrees), glm::vec3(0, 0, 1));
        addMesh(w, makeFloor(8, 20.0f), glm::vec3(0.0f), glm::vec3(1.0f), tilt);
        const glm::vec3 up = tilt * glm::vec3(0, 1, 0);
        const EntityHandle box = addBox(w, up * 0.5f + glm::vec3(0, 0.02f, 0), glm::vec3(0.1f, 0.1f, 0.1f), true);
        w.setPosition(box, glm::vec3(0.0f, 0.0f, 0.0f) + up * 0.12f);
        const glm::vec3 start = w.getWorldPosition(box);
        run(w, 240);
        const float moved = glm::length(w.getWorldPosition(box) - start);
        if (degrees < 30.0f) CHECK(moved < 0.05f);
        else CHECK(moved > 1.0f);
    }
}

void testCompoundBodies() {
    World w;
    setup(w);
    addPlane(w, glm::vec3(0, 1, 0));
    const EntityHandle bell = makeEntity(w, glm::vec3(0.0f, 4.0f, 0.0f));
    CompoundCollider* compound = w.add<CompoundCollider>(bell);
    compound->parts.push_back({CompoundPart::Kind::Sphere, glm::vec3(-1.0f, 0.0f, 0.0f), 0.5f, glm::vec3(0.5f), 0.5f});
    compound->parts.push_back({CompoundPart::Kind::Box, glm::vec3(1.0f, 0.0f, 0.0f), 0.5f, glm::vec3(0.25f, 0.4f, 0.25f), 0.5f});
    w.add<RigidBody>(bell);
    World w2;
    setup(w2);
    addMesh(w2, makeFloor(10, 10.0f));
    const EntityHandle bell2 = makeEntity(w2, glm::vec3(0.0f, 4.0f, 0.0f));
    CompoundCollider* compound2 = w2.add<CompoundCollider>(bell2);
    compound2->parts = compound->parts;
    w2.add<RigidBody>(bell2);
    run(w, 400);
    run(w2, 400);
    CHECK(near(w.getWorldPosition(bell).y, 0.5f, 0.02f));
    CHECK(near(w2.getWorldPosition(bell2).y, 0.5f, 0.02f));
    CHECK(near(w2.getWorldPosition(bell2).x, 0.0f, 0.05f));
    const auto hit = physicsRaycast(w, glm::vec3(1.0f, 5.0f, 0.0f), glm::vec3(0, -1, 0), 20.0f);
    CHECK(hit && hit->entity == bell && near(hit->distance, 5.0f - (w.getWorldPosition(bell).y + 0.4f), 0.03f));
    const auto miss = physicsRaycast(w, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0, -1, 0), 20.0f);
    CHECK(miss && miss->entity != bell);
    const EntityHandle ball = addSphere(w, glm::vec3(1.0f, 3.0f, 0.0f), 0.2f);
    run(w, 400);
    CHECK(w.getWorldPosition(ball).y > 0.8f);
}

void testConvexHull() {
    const auto cube = makeConvexHull(cubePoints(0.5f));
    CHECK(cube && cube->faces.size() == 6 && cube->edges.size() == 12);
    CHECK(!makeConvexHull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}));
    CHECK(!makeConvexHull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}));
    const auto octa = makeConvexHull({{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}});
    CHECK(octa && octa->faces.size() == 8);

    World w;
    setup(w);
    addMesh(w, makeFloor(10, 10.0f));
    addPlane(w, glm::vec3(0, 1, 0), glm::vec3(0, -50, 0));
    const EntityHandle block = makeEntity(w, glm::vec3(0.0f, 3.0f, 0.0f));
    w.add<ConvexHullCollider>(block, cube);
    w.add<RigidBody>(block);
    const EntityHandle gem = makeEntity(w, glm::vec3(4.0f, 3.0f, 0.0f));
    w.add<ConvexHullCollider>(gem, octa);
    w.add<RigidBody>(gem);
    const EntityHandle big = makeEntity(w, glm::vec3(-4.0f, 3.0f, 0.0f), glm::vec3(2.0f));
    w.setRotation(big, glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0)));
    w.add<ConvexHullCollider>(big, cube);
    w.add<RigidBody>(big);
    run(w, 600);
    CHECK(near(w.getWorldPosition(block).y, 0.5f, 0.02f));
    CHECK(near(w.getWorldPosition(gem).y, 1.0f, 0.02f));
    CHECK(near(w.getWorldPosition(big).y, 1.0f, 0.03f));
    const EntityHandle top = addBox(w, glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.3f), true);
    run(w, 600);
    CHECK(near(w.getWorldPosition(top).y, 1.3f, 0.03f));
    const auto hit = physicsRaycast(w, glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0, -1, 0), 50.0f, [&](EntityHandle e) { return e == top; });
    CHECK(hit && hit->entity == block && near(hit->distance, 10.0f - 1.0f, 0.03f));
    const auto sweep = physicsSweepSphere(w, glm::vec3(4.0f, 10.0f, 0.0f), glm::vec3(0, -1, 0), 50.0f, 0.25f);
    CHECK(sweep && sweep->entity == gem && sweep->distance > 7.2f && sweep->distance < 7.8f);
    CHECK(!physicsOverlapCapsule(w, glm::vec3(4.0f, 0.5f, 0.0f), glm::vec3(4.0f, 1.5f, 0.0f), 0.3f).empty());
    CHECK(!physicsOverlapBox(w, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.4f)).empty());
}

void testCapsuleCollider() {
    World w;
    setup(w);
    addPlane(w, glm::vec3(0, 1, 0));
    const EntityHandle crate = addBox(w, glm::vec3(5.0f, 0.5f, 0.0f), glm::vec3(0.5f));
    const EntityHandle pill = makeEntity(w, glm::vec3(0.0f, 3.0f, 0.0f));
    w.add<CapsuleCollider>(pill, 0.3f, 0.5f);
    w.add<RigidBody>(pill);
    const EntityHandle pill2 = makeEntity(w, glm::vec3(5.0f, 5.0f, 0.0f));
    w.add<CapsuleCollider>(pill2, 0.3f, 0.5f);
    w.add<RigidBody>(pill2);
    run(w, 480);
    CHECK(near(w.getWorldPosition(pill).y, 0.8f, 0.01f));
    CHECK(near(w.getWorldPosition(pill2).y, 1.0f + 0.8f, 0.02f));
    (void)crate;
    const EntityHandle ball = addSphere(w, glm::vec3(0.0f, 4.0f, 0.0f), 0.2f);
    run(w, 480);
    CHECK(w.getWorldPosition(ball).y > 1.0f);
    World q;
    setup(q);
    const EntityHandle c = makeEntity(q, glm::vec3(0.0f, 1.0f, 0.0f));
    q.add<CapsuleCollider>(c, 0.3f, 0.5f);
    const auto side = physicsRaycast(q, glm::vec3(5, 1, 0), glm::vec3(-1, 0, 0), 20.0f);
    CHECK(side && near(side->distance, 4.7f, 1e-3f) && side->normal.x > 0.99f);
    const auto cap = physicsRaycast(q, glm::vec3(0, 6, 0), glm::vec3(0, -1, 0), 20.0f);
    CHECK(cap && near(cap->distance, 6.0f - 1.8f, 1e-3f) && cap->normal.y > 0.99f);
    CHECK(!physicsRaycast(q, glm::vec3(0, 6, 1), glm::vec3(0, -1, 0), 20.0f));
    const auto sweep = physicsSweepSphere(q, glm::vec3(-5, 1, 0), glm::vec3(1, 0, 0), 20.0f, 0.2f);
    CHECK(sweep && near(sweep->distance, 5.0f - 0.5f, 1e-3f));
    const auto overlap = physicsOverlapCapsule(q, glm::vec3(0.5f, 0.6f, 0.0f), glm::vec3(0.5f, 1.4f, 0.0f), 0.3f);
    CHECK(overlap.size() == 1 && near(overlap[0].depth, 0.1f, 1e-3f) && overlap[0].normal.x > 0.99f);
    CHECK(physicsOverlapCapsule(q, glm::vec3(0.7f, 0.6f, 0.0f), glm::vec3(0.7f, 1.4f, 0.0f), 0.3f).empty());
    const auto boxHit = physicsOverlapBox(q, glm::vec3(0.7f, 1.0f, 0.0f), glm::vec3(0.5f));
    CHECK(boxHit.size() == 1 && near(boxHit[0].depth, 0.1f, 1e-3f));
}

void testFriction() {
    World w;
    setup(w);
    addPlane(w, glm::vec3(0, 1, 0));
    const EntityHandle box = addBox(w, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f), true);
    w.get<RigidBody>(box)->velocity = glm::vec3(3.0f, 0.0f, 0.0f);
    PhysicsSettings settings;
    run(w, 360, 1.0f / 240.0f, settings);
    const float travelled = w.getWorldPosition(box).x;
    CHECK(travelled > 0.8f && travelled < 1.05f);
    CHECK(glm::length(w.get<RigidBody>(box)->velocity) < 0.05f);
    World f;
    setup(f);
    addPlane(f, glm::vec3(0, 1, 0));
    const EntityHandle slick = addBox(f, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f), true);
    f.get<RigidBody>(slick)->velocity = glm::vec3(3.0f, 0.0f, 0.0f);
    f.get<RigidBody>(slick)->friction = 0.0f;
    f.get<RigidBody>(slick)->staticFriction = 0.0f;
    run(f, 240, 1.0f / 240.0f, settings);
    CHECK(near(f.get<RigidBody>(slick)->velocity.x, 3.0f, 0.01f));
    World g;
    setup(g);
    const EntityHandle floor = addPlane(g, glm::vec3(0, 1, 0));
    g.get<PlaneCollider>(floor)->friction = 1.0f;
    const EntityHandle grippy = addBox(g, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f), true);
    g.get<RigidBody>(grippy)->friction = 1.0f;
    g.get<RigidBody>(grippy)->velocity = glm::vec3(3.0f, 0.0f, 0.0f);
    run(g, 360, 1.0f / 240.0f, settings);
    CHECK(g.getWorldPosition(grippy).x < travelled * 0.7f);
    for (const float degrees : {20.0f, 40.0f}) {
        World s;
        setup(s);
        const float a = glm::radians(degrees);
        addPlane(s, glm::vec3(std::sin(a), std::cos(a), 0.0f));
        const glm::vec3 n(std::sin(a), std::cos(a), 0.0f);
        const float reach = 0.5f * (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
        const EntityHandle block = addBox(s, n * 0.0f + glm::vec3(0.0f, reach / n.y, 0.0f), glm::vec3(0.5f), true);
        const glm::vec3 start = s.getWorldPosition(block);
        run(s, 480, 1.0f / 240.0f, settings);
        const float moved = glm::length(s.getWorldPosition(block) - start);
        if (degrees < 30.0f) CHECK(moved < 0.03f);
        else CHECK(moved > 0.5f);
    }
    World d;
    setup(d);
    addPlane(d, glm::vec3(0, 1, 0));
    d.get<PlaneCollider>(addPlane(d, glm::vec3(0, 1, 0), glm::vec3(0, -10, 0)))->friction = 0.0f;
    const EntityHandle lower = addBox(d, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.5f), true);
    d.get<RigidBody>(lower)->friction = 0.0f;
    d.get<RigidBody>(lower)->staticFriction = 0.0f;
    const EntityHandle upper = addBox(d, glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.5f), true);
    d.get<RigidBody>(lower)->velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    d.get<RigidBody>(upper)->friction = 0.8f;
    d.get<RigidBody>(upper)->staticFriction = 0.8f;
    d.get<RigidBody>(lower)->friction = 0.8f;
    d.get<RigidBody>(lower)->staticFriction = 0.8f;
    float fastest = 0.0f;
    for (int i = 0; i < 120; ++i) {
        stepPhysics(d, 1.0f / 240.0f, settings);
        fastest = std::max(fastest, d.get<RigidBody>(upper)->velocity.x);
    }
    CHECK(fastest > 0.1f);
}

void testRolling() {
    const float dt = 1.0f / 240.0f;
    PhysicsSettings settings;
    World w;
    setup(w);
    addPlane(w, glm::vec3(0, 1, 0));
    const EntityHandle ball = addSphere(w, glm::vec3(0.0f, 0.5f, 0.0f), 0.5f);
    w.get<RigidBody>(ball)->velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    run(w, 360, dt, settings);
    const RigidBody* rb = w.get<RigidBody>(ball);
    CHECK(near(rb->velocity.x, 2.0f * 5.0f / 7.0f, 0.06f));
    CHECK(near(-rb->angularVelocity.z * 0.5f, rb->velocity.x, 0.06f));
    CHECK(std::abs(rb->angularVelocity.x) < 0.01f);
    CHECK(glm::length(glm::vec3(w.getRotation(ball).x, w.getRotation(ball).y, w.getRotation(ball).z)) > 0.05f);
    run(w, 480, dt, settings);
    CHECK(near(w.get<RigidBody>(ball)->velocity.x, 2.0f * 5.0f / 7.0f, 0.08f));

    World r;
    setup(r);
    addPlane(r, glm::vec3(0, 1, 0));
    const EntityHandle slow = addSphere(r, glm::vec3(0.0f, 0.5f, 0.0f), 0.5f);
    RigidBody* body = r.get<RigidBody>(slow);
    body->velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    body->rollingResistance = 0.05f;
    run(r, 1440, dt, settings);
    CHECK(glm::length(r.get<RigidBody>(slow)->velocity) < 0.2f);
    CHECK(r.getWorldPosition(slow).x < w.getWorldPosition(ball).x);

    World s;
    setup(s);
    const float a = glm::radians(20.0f);
    addPlane(s, glm::vec3(std::sin(a), std::cos(a), 0.0f));
    const EntityHandle rolling = addSphere(s, glm::vec3(0.0f, 0.5f / std::cos(a), 0.0f), 0.5f);
    run(s, 480, dt, settings);
    CHECK(s.getWorldPosition(rolling).x > 1.0f);
    CHECK(std::abs(s.get<RigidBody>(rolling)->angularVelocity.z) > 1.0f);
}

void testRotatedHeightfield() {
    auto data = std::make_shared<HeightfieldData>();
    data->samplesX = 5;
    data->samplesZ = 5;
    data->cellSize = 1.0f;
    data->heights.assign(25, 0.0f);
    data->updateBounds();
    World w;
    setup(w);
    const EntityHandle wall = makeEntity(w, glm::vec3(0.0f));
    w.setRotation(wall, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)));
    w.add<HeightfieldCollider>(wall, data);
    const auto hit = physicsRaycast(w, glm::vec3(-3.0f, 2.0f, 2.0f), glm::vec3(1, 0, 0), 20.0f);
    CHECK(hit && hit->entity == wall && near(hit->distance, 3.0f, 1e-3f) && hit->normal.x < -0.99f);
    CHECK(!physicsRaycast(w, glm::vec3(-3.0f, 6.0f, 2.0f), glm::vec3(1, 0, 0), 20.0f));
    const auto sweep = physicsSweepSphere(w, glm::vec3(-3.0f, 2.0f, 2.0f), glm::vec3(1, 0, 0), 20.0f, 0.5f);
    CHECK(sweep && near(sweep->distance, 2.5f, 0.02f));
    const auto overlap = physicsOverlapCapsule(w, glm::vec3(-0.3f, 2.0f, 2.0f), glm::vec3(-0.3f, 2.0f, 2.0f), 0.5f);
    CHECK(overlap.size() == 1 && near(overlap[0].depth, 0.2f, 1e-3f) && overlap[0].normal.x < -0.99f);
    World dyn;
    setup(dyn);
    PhysicsSettings noGravity;
    noGravity.gravity = glm::vec3(0.0f);
    const EntityHandle wall2 = makeEntity(dyn, glm::vec3(0.0f));
    dyn.setRotation(wall2, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)));
    dyn.add<HeightfieldCollider>(wall2, data);
    const EntityHandle ball = addSphere(dyn, glm::vec3(-3.0f, 2.0f, 2.0f), 0.5f);
    dyn.get<RigidBody>(ball)->velocity = glm::vec3(6.0f, 0.0f, 0.0f);
    run(dyn, 240, 1.0f / 120.0f, noGravity);
    CHECK(dyn.getWorldPosition(ball).x < -0.45f && dyn.getWorldPosition(ball).x > -0.6f);

    auto raised = std::make_shared<HeightfieldData>(*data);
    std::fill(raised->heights.begin(), raised->heights.end(), 1.0f);
    raised->updateBounds();
    World s;
    setup(s);
    const EntityHandle field = makeEntity(s, glm::vec3(0.0f), glm::vec3(2.0f));
    s.add<HeightfieldCollider>(field, raised);
    const auto down = physicsRaycast(s, glm::vec3(7.0f, 5.0f, 7.0f), glm::vec3(0, -1, 0), 20.0f);
    CHECK(down && near(down->distance, 3.0f, 1e-3f));
    CHECK(!physicsRaycast(s, glm::vec3(9.0f, 5.0f, 7.0f), glm::vec3(0, -1, 0), 20.0f));
    const EntityHandle ball2 = addSphere(s, glm::vec3(6.0f, 4.0f, 6.0f), 0.5f);
    run(s, 480);
    CHECK(near(s.getWorldPosition(ball2).y, 2.5f, 0.03f));
    const EntityHandle crate = addBox(s, glm::vec3(2.0f, 4.0f, 2.0f), glm::vec3(0.5f), true);
    run(s, 480);
    CHECK(near(s.getWorldPosition(crate).y, 2.5f, 0.05f));
}

const char* kPortals = R"({
  "cells": [
    {"name": "X", "min": [-30, -1, -30], "max": [30, 8, 30]},
    {"name": "Y", "min": [70, -1, -30], "max": [130, 8, 30]}
  ],
  "portals": [
    {"name": "turn", "a": {"cell": "X", "position": [10, 1.5, 0], "rotation": [-90, 0, 0], "size": [1.5, 1.5]},
                     "b": {"cell": "Y", "position": [100, 1.5, 0], "rotation": [0, 0, 0], "size": [1.5, 1.5]}}
  ]
})";

void testPortalPhysics() {
    World w;
    setup(w);
    PortalSystem p;
    CHECK(p.parseJson(kPortals));
    installPortalPhysics(p);
    PhysicsSettings noGravity;
    noGravity.gravity = glm::vec3(0.0f);
    const Portal& A = p.portals()[static_cast<size_t>(p.portals()[0].cell == p.findCell("X") ? 0 : 1)];
    const glm::vec3 n = A.normal();
    const glm::mat3 rotation(A.transform);

    const glm::vec3 origin = glm::vec3(A.center().x, 1.5f, A.center().z) + n * 4.0f;
    const PortalCast cast = portalSweep(p, w, origin, -n, 8.0f, 0.2f, p.cellAt(origin), {}, false);
    CHECK(cast.crossings == 1);
    CHECK(glm::length(cast.transformVector(-n) - cast.direction) < 1e-3f);
    CHECK(glm::length(cast.transformVector(-n) - rotation * (-n)) < 1e-3f);
    CHECK(portalSweep(p, w, origin, -n, 3.0f, 0.2f, p.cellAt(origin), {}, false).transform == glm::mat4(1.0f));

    const Portal& B = p.portals()[static_cast<size_t>(A.partner)];
    const glm::vec3 exitDir = rotation * (-n);
    const EntityHandle post = addSphere(w, B.center() + exitDir * 0.6f, 0.4f, false);
    const glm::vec3 low = glm::vec3(A.center().x, 1.0f, A.center().z) - n * 0.15f;
    const auto overlaps = portalOverlapCapsule(p, w, low, low + glm::vec3(0, 1, 0), 0.3f, p.findCell("X"));
    bool found = false;
    for (const PortalOverlap& o : overlaps) found = found || (o.entity == post && o.crossings == 1);
    CHECK(found);
    CHECK(portalOverlapCapsule(p, w, low + n * 8.0f, low + n * 8.0f + glm::vec3(0, 1, 0), 0.3f, p.findCell("X")).empty());

    for (int kind = 0; kind < 3; ++kind) {
        World b;
        setup(b);
        PortalSystem q;
        CHECK(q.parseJson(kPortals));
        installPortalPhysics(q);
        const Portal& QA = q.portals()[static_cast<size_t>(q.portals()[0].cell == q.findCell("X") ? 0 : 1)];
        const glm::vec3 qn = QA.normal();
        const EntityHandle body = makeEntity(b, glm::vec3(QA.center().x, 1.5f, QA.center().z) + qn * 2.0f);
        if (kind == 0) {
            b.add<SphereCollider>(body, 0.3f);
        } else if (kind == 1) {
            b.add<CapsuleCollider>(body, 0.25f, 0.3f);
        } else {
            CompoundCollider* compound = b.add<CompoundCollider>(body);
            compound->parts.push_back({CompoundPart::Kind::Sphere, glm::vec3(0.0f), 0.25f, glm::vec3(0.5f), 0.5f});
            compound->parts.push_back({CompoundPart::Kind::Sphere, glm::vec3(0.0f, 0.4f, 0.0f), 0.2f, glm::vec3(0.5f), 0.5f});
        }
        RigidBody* rb = b.add<RigidBody>(body);
        rb->velocity = -qn * 6.0f;
        rb->angularVelocity = glm::vec3(0.0f, 2.0f, 0.0f);
        rb->friction = 0.0f;
        q.track(b, body);
        bool crossed = false;
        glm::vec3 vBefore(0.0f);
        glm::vec3 wBefore(0.0f);
        for (int i = 0; i < 240 && !crossed; ++i) {
            vBefore = b.get<RigidBody>(body)->velocity;
            wBefore = b.get<RigidBody>(body)->angularVelocity;
            const int cellBefore = q.objectCell(body);
            stepPortalPhysics(q, b, 1.0f / 120.0f, noGravity);
            crossed = q.objectCell(body) != cellBefore;
        }
        CHECK(crossed);
        const glm::mat3 qr(QA.transform);
        const glm::vec3 vAfter = b.get<RigidBody>(body)->velocity;
        CHECK(glm::length(vAfter - qr * vBefore) < 1e-3f);
        CHECK(near(glm::length(vAfter), 6.0f, 1e-3f));
        if (kind == 0) CHECK(glm::length(b.get<RigidBody>(body)->angularVelocity - qr * wBefore) < 1e-3f);
        const glm::vec3 pos = b.getWorldPosition(body);
        for (int i = 0; i < 60; ++i) stepPortalPhysics(q, b, 1.0f / 120.0f, noGravity);
        const glm::vec3 moved = b.getWorldPosition(body) - pos;
        CHECK(glm::dot(glm::normalize(moved), glm::normalize(vAfter)) > 0.99f);
    }
}

}

int main() {
    testMeshQueries();
    testMeshTransform();
    testDynamicOnMesh();
    testMeshSlopeFriction();
    testCompoundBodies();
    testConvexHull();
    testCapsuleCollider();
    testFriction();
    testRolling();
    testRotatedHeightfield();
    testPortalPhysics();
    return testsupport::finish();
}
