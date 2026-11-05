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
import Engine.Rng;

#include "WorldFixtures.hpp"
#include "WorldSuites.hpp"

namespace {

void testSpatial() {
    World world;
    world.setMeshBoundsHook([](uint32_t mesh) { return mesh == 99 ? glm::vec4(0.0f, 0.0f, 0.0f, 1000.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    auto near = world.create("near", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    auto far = world.create("far", 1, glm::vec3(0.0f, 0.0f, -50.0f));
    world.create("side", 1, glm::vec3(100.0f, 0.0f, 0.0f));

    auto hit = world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == near);
    CHECK(hit && std::abs(hit->distance - 9.0f) < 0.001f);
    CHECK(!world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(!world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 5.0f));

    CHECK(world.overlapSphere(glm::vec3(0.0f), 12.0f).size() == 1);
    CHECK(world.overlapSphere(glm::vec3(0.0f), 60.0f).size() == 2);
    CHECK(world.overlapSphere(glm::vec3(100.0f, 0.0f, 0.0f), 0.5f).size() == 1);

    world.setPosition(near, glm::vec3(0.0f, 30.0f, -10.0f));
    hit = world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == far);

    world.setVisible(far, false);
    CHECK(!world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
    world.setVisible(far, true);
    world.destroy(far);
    CHECK(!world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));

    auto parent = world.create("parent", 0, glm::vec3(0.0f, 0.0f, -200.0f));
    auto child = world.create("child", 1, glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(1.0f), parent);
    world.setVisible(parent, false);
    hit = world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == child);
    world.setPosition(parent, glm::vec3(500.0f, 0.0f, -200.0f));
    CHECK(!world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
    hit = world.raycast(glm::vec3(500.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == child);

    world.create("ground", 99);
    CHECK(world.overlapSphere(glm::vec3(0.0f, -900.0f, 0.0f), 1.0f).size() == 1);
}

void testScreenRay() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    world.camera().setPos(glm::vec3(0.0f, 0.0f, 10.0f));
    world.camera().setOrientation(-90.0f, 0.0f);
    world.camera().updateAspectRatio(1280.0f, 720.0f);
    const Ray center = world.screenRay(640.0f, 360.0f, 1280.0f, 720.0f);
    CHECK(std::abs(center.direction.z + 1.0f) < 0.001f);
    CHECK(std::abs(center.direction.x) < 0.001f && std::abs(center.direction.y) < 0.001f);
    const Ray right = world.screenRay(1000.0f, 360.0f, 1280.0f, 720.0f);
    CHECK(right.direction.x > 0.05f);
    const Ray top = world.screenRay(640.0f, 100.0f, 1280.0f, 720.0f);
    CHECK(top.direction.y > 0.05f);

    auto target = world.create("target", 1, glm::vec3(0.0f, 0.0f, 0.0f));
    auto hit = world.raycast(center.origin, center.direction);
    CHECK(hit && hit->entity == target);
    auto offside = world.create("offside", 1, glm::vec3(3.0f, 0.0f, 0.0f));
    const Ray toOffside = world.screenRay(640.0f + 640.0f * (3.0f / 10.0f) / std::tan(glm::radians(22.5f)) / (1280.0f / 720.0f), 360.0f, 1280.0f, 720.0f);
    hit = world.raycast(toOffside.origin, toOffside.direction);
    CHECK(hit && hit->entity == offside);
}

void testFrustumQuery() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    world.camera().setPos(glm::vec3(0.0f));
    world.camera().setOrientation(-90.0f, 0.0f);
    world.camera().updateAspectRatio(16.0f, 9.0f);
    world.camera().setNearPlane(0.1f);
    world.camera().setFarPlane(100.0f);

    auto front = world.create("front", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    auto behind = world.create("behind", 1, glm::vec3(0.0f, 0.0f, 10.0f));
    auto beyond = world.create("beyond", 1, glm::vec3(0.0f, 0.0f, -200.0f));
    auto wide = world.create("wide", 1, glm::vec3(60.0f, 0.0f, -10.0f));
    auto edge = world.create("edge", 1, glm::vec3(7.6f, 0.0f, -10.0f));
    auto above = world.create("above", 1, glm::vec3(0.0f, 30.0f, -10.0f));

    const auto visible = world.queryFrustum(world.camera());
    const auto has = [&](EntityHandle e) { return std::find(visible.begin(), visible.end(), e) != visible.end(); };
    CHECK(has(front));
    CHECK(has(edge));
    CHECK(!has(behind));
    CHECK(!has(beyond));
    CHECK(!has(wide));
    CHECK(!has(above));
    CHECK(visible.size() == 2);

    world.camera().setOrientation(90.0f, 0.0f);
    const auto turned = world.queryFrustum(world.camera());
    CHECK(turned.size() == 1 && turned[0] == behind);
}

void testJobsAndParallelSpatial() {
    JobSystem jobs(3);
    CHECK(jobs.workerCount() == 3);
    std::vector<int> hits(100000, 0);
    jobs.parallelFor(hits.size(), 1000, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) hits[i] += 1;
    });
    CHECK(std::all_of(hits.begin(), hits.end(), [](int v) { return v == 1; }));

    std::atomic<size_t> total{0};
    for (int round = 0; round < 50; ++round) {
        jobs.parallelFor(1000, 7, [&](size_t b, size_t e) { total += e - b; });
    }
    CHECK(total == 50000);

    size_t serial = 0;
    JobSystem none(0);
    none.parallelFor(10, 3, [&](size_t b, size_t e) { serial += e - b; });
    CHECK(serial == 10);
    jobs.parallelFor(0, 10, [&](size_t, size_t) { serial += 1000; });
    CHECK(serial == 10);
    jobs.parallelFor(5, 100, [&](size_t b, size_t e) { serial += e - b; });
    CHECK(serial == 15);

    const auto build = [](World& world) {
        world.setMeshBoundsHook([](uint32_t mesh) { return mesh == 9 ? glm::vec4(0.0f, 0.0f, 0.0f, 500.0f) : glm::vec4(0.5f, 0.0f, 0.0f, 1.0f); });
        uint32_t seed = 12345;
        const auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
        };
        std::vector<EntityHandle> made;
        for (int i = 0; i < 20000; ++i) {
            EntityHandle parent = (i % 7 == 0 && !made.empty()) ? made[made.size() / 2] : NULL_ENTITY;
            made.push_back(world.create("e", i % 500 == 0 ? 9 : 1, glm::vec3(rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f), glm::vec3(0.5f + rnd()), parent));
            if (i % 13 == 0) world.setVisible(made.back(), false);
        }
        for (int i = 0; i < 300; ++i) world.destroy(made[static_cast<size_t>(i) * 50 % made.size()]);
    };

    World threaded;
    JobSystem pool(4);
    threaded.setJobSystem(&pool);
    World plain;
    build(threaded);
    build(plain);

    const auto sorted = [](std::vector<EntityHandle> v) {
        std::sort(v.begin(), v.end(), [](const EntityHandle& a, const EntityHandle& b) { return a.index < b.index; });
        return v;
    };
    uint32_t seed = 777;
    const auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    bool sameOverlap = true;
    bool sameRay = true;
    for (int q = 0; q < 40; ++q) {
        const glm::vec3 c(rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f);
        sameOverlap = sameOverlap && sorted(threaded.overlapSphere(c, 15.0f)) == sorted(plain.overlapSphere(c, 15.0f));
        const glm::vec3 dir = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f));
        const auto ta = threaded.raycast(c, dir);
        const auto pa = plain.raycast(c, dir);
        sameRay = sameRay && ta.has_value() == pa.has_value() && (!ta || (ta->entity == pa->entity && ta->distance == pa->distance));
    }
    CHECK(sameOverlap);
    CHECK(sameRay);

    if (std::getenv("LIT_PERF")) {
        World big;
        big.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        JobSystem bigPool;
        big.createBatch(1000000, [](size_t i, EntityDesc& d) {
            d.mesh = 1;
            d.position = glm::vec3(float(i % 100) * 3.0f, float((i / 100) % 100) * 3.0f, float(i / 10000) * 3.0f);
        });
        const auto t0 = std::chrono::steady_clock::now();
        (void)big.raycast(glm::vec3(0.0f, 500.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        const auto t1 = std::chrono::steady_clock::now();
        World big2;
        big2.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        big2.setJobSystem(&bigPool);
        big2.createBatch(1000000, [](size_t i, EntityDesc& d) {
            d.mesh = 1;
            d.position = glm::vec3(float(i % 100) * 3.0f, float((i / 100) % 100) * 3.0f, float(i / 10000) * 3.0f);
        });
        const auto t2 = std::chrono::steady_clock::now();
        (void)big2.raycast(glm::vec3(0.0f, 500.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        const auto t3 = std::chrono::steady_clock::now();
        std::printf("spatial first build 1M: serial %.1f ms, %zu workers %.1f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count(), bigPool.workerCount(), std::chrono::duration<double, std::milli>(t3 - t2).count());
    }
}

void testFramingAndSceneBounds() {
    const glm::vec4 one = gizmo::enclosingSphere({glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)});
    CHECK(nearlyEqual(glm::vec3(one), glm::vec3(1.0f, 2.0f, 3.0f)) && std::abs(one.w - 4.0f) < 1.0e-4f);
    const glm::vec4 two = gizmo::enclosingSphere({glm::vec4(-10.0f, 0.0f, 0.0f, 1.0f), glm::vec4(10.0f, 0.0f, 0.0f, 1.0f)});
    CHECK(nearlyEqual(glm::vec3(two), glm::vec3(0.0f)) && std::abs(two.w - 11.0f) < 1.0e-3f);
    CHECK(gizmo::enclosingSphere({}).w == 0.0f);
    const glm::vec4 nested = gizmo::enclosingSphere({glm::vec4(0.0f, 0.0f, 0.0f, 10.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
    CHECK(nested.w >= 10.0f && nested.w < 11.5f);

    const float d1 = gizmo::frameDistance(1.0f, 45.0f, 16.0f / 9.0f);
    const float d2 = gizmo::frameDistance(2.0f, 45.0f, 16.0f / 9.0f);
    CHECK(std::abs(d2 - 2.0f * d1) < 1.0e-3f);
    const float tall = gizmo::frameDistance(1.0f, 45.0f, 0.5f);
    CHECK(tall > d1);
    CHECK(std::abs(d1 - 1.1f / std::sin(glm::radians(22.5f))) < 1.0e-3f);

    World world;
    CHECK(!world.sceneBounds());
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    world.create("empty");
    CHECK(!world.sceneBounds());
    world.create("a", 1, glm::vec3(-10.0f, 0.0f, 0.0f));
    world.create("b", 1, glm::vec3(10.0f, 0.0f, 0.0f));
    const auto bounds = world.sceneBounds();
    CHECK(bounds && nearlyEqual(glm::vec3(*bounds), glm::vec3(0.0f)) && bounds->w > 10.0f && bounds->w < 20.0f);
}

void testTrimAndCompact() {
    World world;
    std::vector<EntityHandle> handles;
    for (int i = 0; i < 100; ++i) handles.push_back(world.create("e" + std::to_string(i), 1, glm::vec3(float(i), 0.0f, 0.0f)));
    for (int i = 50; i < 100; ++i) world.destroy(handles[i]);
    CHECK(world.database().transforms.size() == 50);
    CHECK(world.aliveCount() == 50);
    CHECK(!world.isAlive(handles[99]));
    auto fresh = world.create("fresh", 1);
    CHECK(fresh.index == 50);
    CHECK(fresh.generation > handles[50].generation);
    CHECK(!world.isAlive(handles[50]));
    world.destroy(fresh);

    for (int i = 0; i < 50; i += 2) world.destroy(handles[i]);
    CHECK(world.aliveCount() == 25);
    CHECK(world.database().transforms.size() > 25);

    auto parent = world.find("e49");
    auto childOfLast = world.create("child", 1, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), parent);
    world.add<Health>(childOfLast, 7);
    world.setTag(childOfLast, "moved");
    world.setVisible(childOfLast, false);

    const auto moved = world.compact();
    CHECK(!moved.empty());
    CHECK(world.database().transforms.size() == world.aliveCount());
    CHECK(world.database().hierarchies.size() == world.aliveCount());
    CHECK(!world.isAlive(childOfLast));

    auto rc = world.find("child");
    auto rp = world.find("e49");
    CHECK(!rc.isNull() && !rp.isNull());
    CHECK(world.getParent(rc) == rp);
    CHECK(world.getChildren(rp).size() == 1);
    CHECK(world.get<Health>(rc) && world.get<Health>(rc)->hp == 7);
    CHECK(world.findByTag("moved").size() == 1 && world.findByTag("moved")[0] == rc);
    CHECK(!world.isVisible(rc));
    CHECK(world.getWorldPosition(rc).x == 49.0f && world.getWorldPosition(rc).y == 5.0f);
    CHECK(world.database().renderables[rc.index].objectId == rc.index);
    for (const EntityMoved& m : moved) {
        CHECK(!world.isAlive(m.from));
        CHECK(world.isAlive(m.to));
    }
    size_t roots = world.getRoots().size();
    CHECK(roots == world.aliveCount() - 1);
}

void testCompactKeepsSpatialAndScripts() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    auto a = world.create("a", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    auto b = world.create("b", 1, glm::vec3(0.0f, 0.0f, -30.0f));
    auto c = world.create("c", 1, glm::vec3(0.0f, 0.0f, -50.0f));
    CHECK(world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f))->entity == a);
    int starts = 0, updates = 0, destroys = 0;
    world.attach<Counter>(c, &starts, &updates, &destroys);
    world.setActiveCamera(c);
    world.destroy(a);
    world.destroy(b);
    world.compact();
    auto rc = world.find("c");
    CHECK(rc.index == 0);
    auto hit = world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == rc);
    CHECK(world.getActiveCamera() == rc);
    world.update(0.016f);
    CHECK(starts == 1 && updates == 1 && destroys == 1);
    CHECK(world.aliveCount() == 0);
}

void testBatch() {
    World world;
    std::vector<EntityHandle> handles;
    for (int i = 0; i < 6000; ++i) handles.push_back(world.create("e", 1, glm::vec3(float(i), 0.0f, 0.0f)));

    std::vector<std::pair<EntityHandle, glm::vec3>> moves;
    for (int i = 0; i < 6000; ++i) moves.emplace_back(handles[i], glm::vec3(0.0f, float(i), 1.0f));
    world.setPositions(moves);
    CHECK(world.getPosition(handles[5999]).y == 5999.0f);
    CHECK(world.getPosition(handles[0]).x == 0.0f && world.getPosition(handles[0]).z == 1.0f);

    std::vector<std::pair<EntityHandle, glm::vec3>> few{{handles[10], glm::vec3(7.0f)}, {NULL_ENTITY, glm::vec3(1.0f)}};
    world.setPositions(few);
    CHECK(world.getPosition(handles[10]).x == 7.0f);

    std::vector<EntityHandle> doomed(handles.begin() + 3000, handles.end());
    doomed.push_back(handles[3000]);
    world.destroyBatch(doomed);
    CHECK(world.aliveCount() == 3000);
    CHECK(world.database().transforms.size() == 3000);
    CHECK(!world.isAlive(handles[4000]));
    CHECK(world.isAlive(handles[2999]));
}

}

void testQueriesMatchBruteForce() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    Pcg32 rng(77);
    std::vector<EntityHandle> handles;
    for (int index = 0; index < 3000; ++index) {
        handles.push_back(world.create("e", 1, glm::vec3(rng.uniform() * 300.0f - 150.0f, rng.uniform() * 60.0f - 30.0f, rng.uniform() * 300.0f - 150.0f)));
    }
    std::vector<EntityHandle> scratch;
    for (int query = 0; query < 60; ++query) {
        const glm::vec3 center(rng.uniform() * 300.0f - 150.0f, rng.uniform() * 60.0f - 30.0f, rng.uniform() * 300.0f - 150.0f);
        const float radius = 1.0f + rng.uniform() * 40.0f;
        world.overlapSphere(center, radius, scratch);
        std::vector<uint32_t> expected;
        for (EntityHandle handle : handles) {
            const glm::vec4 bounds = world.getWorldBounds(handle);
            if (glm::length(glm::vec3(bounds) - center) <= radius + bounds.w - 1.0e-3f) expected.push_back(handle.index);
        }
        std::vector<uint32_t> found;
        for (EntityHandle handle : scratch) found.push_back(handle.index);
        std::sort(found.begin(), found.end());
        std::sort(expected.begin(), expected.end());
        CHECK(std::includes(found.begin(), found.end(), expected.begin(), expected.end()));
        CHECK(found.size() <= expected.size() + 4);
        CHECK(world.overlapSphere(center, radius).size() == scratch.size());

        const glm::vec3 direction = glm::normalize(glm::vec3(rng.uniform() - 0.5f, rng.uniform() - 0.5f, rng.uniform() - 0.5f) + glm::vec3(1.0e-3f));
        float bestDistance = 1.0e30f;
        for (EntityHandle handle : handles) {
            const glm::vec4 bounds = world.getWorldBounds(handle);
            const glm::vec3 toCenter = glm::vec3(bounds) - center;
            const float along = glm::dot(toCenter, direction);
            const float perpendicular2 = glm::dot(toCenter, toCenter) - along * along;
            if (perpendicular2 > bounds.w * bounds.w) continue;
            const float half = std::sqrt(bounds.w * bounds.w - perpendicular2);
            const float distance = along - half >= 0.0f ? along - half : along + half;
            if (distance >= 0.0f) bestDistance = std::min(bestDistance, distance);
        }
        const auto hit = world.raycast(center, direction);
        CHECK(hit.has_value() == (bestDistance < 1.0e29f));
        if (hit) CHECK(std::fabs(hit->distance - bestDistance) < 2.0e-2f);
    }
}

void runSpatialTests() {
    testQueriesMatchBruteForce();
    testTrimAndCompact();
    testBatch();
    testFrustumQuery();
    testJobsAndParallelSpatial();
    testFramingAndSceneBounds();
    testCompactKeepsSpatialAndScripts();
    testSpatial();
    testScreenRay();
}
