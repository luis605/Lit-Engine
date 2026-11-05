#include "TestSupport.hpp"
#include "SystemsSuites.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

import Engine.AI;
import Engine.Nav;
import Engine.Rig;
import Engine.Face;
import Engine.Audio;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Params;
import Engine.glm;
import Engine.Json;
import Engine.Rng;

static bool nearlyEqual(float a, float b, float eps = 1.0e-3f) { return std::fabs(a - b) <= eps; }

static TriangleSoup floorWithWall() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(20.0f, 0.5f, 20.0f));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -5.0f)), glm::vec3(0.25f, 1.0f, 15.0f));
    return soup;
}

static void testGeometry() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f)), glm::vec3(1.0f));
    Bvh bvh;
    bvh.build(soup);
    const auto hit = bvh.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(hit && nearlyEqual(hit->t, 4.0f) && nearlyEqual(hit->normal.z, -1.0f));
    CHECK(!bvh.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(bvh.occluded(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 9.0f)));
    CHECK(!bvh.occluded(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 3.0f)));
    std::vector<uint32_t> tris;
    Aabb q;
    q.grow(glm::vec3(-0.1f, -0.1f, 3.9f));
    q.grow(glm::vec3(0.1f, 0.1f, 4.1f));
    bvh.query(q, tris);
    CHECK(!tris.empty());

    World world;
    const auto floor = world.create("floor", NO_MESH, glm::vec3(0.0f, -0.5f, 0.0f));
    world.add<BoxCollider>(floor, glm::vec3(10.0f, 0.5f, 10.0f));
    TriangleSoup fromWorld;
    collectColliderGeometry(world, fromWorld);
    CHECK(fromWorld.triangleCount() == 12);
    CHECK(nearlyEqual(fromWorld.bounds().hi.y, 0.0f));
}

static float pathLength(const NavPath& p) { return p.length; }

static void testNav() {
    NavMesh mesh;
    mesh.setGeometry(floorWithWall());
    mesh.rebuildAll();
    CHECK(mesh.tileCount() > 4 && mesh.walkableCount() > 1000);
    const glm::vec3 from(-5.0f, 0.0f, 0.0f);
    const glm::vec3 to(5.0f, 0.0f, 0.0f);
    const NavPath direct = mesh.findPath(from, to);
    CHECK(direct.found && !direct.partial);
    CHECK(pathLength(direct) > 22.0f && pathLength(direct) < 45.0f);
    bool crosses = false;
    for (size_t i = 1; i < direct.points.size(); ++i) {
        const glm::vec3 a = direct.points[i - 1].position;
        const glm::vec3 b = direct.points[i].position;
        if (a.x * b.x < 0.0f && std::max(a.z, b.z) < 9.5f) crosses = true;
    }
    CHECK(!crosses);

    NavMesh again;
    again.setGeometry(floorWithWall());
    again.rebuildAll();
    CHECK(again.hash() == mesh.hash());

    const uint32_t blocker = mesh.addObstacle([] {
        Aabb b;
        b.grow(glm::vec3(-0.5f, 0.0f, 9.5f));
        b.grow(glm::vec3(0.5f, 2.0f, 20.5f));
        return b;
    }());
    CHECK(mesh.dirtyCount() > 0 && mesh.dirtyCount() < mesh.tileCount());
    const uint64_t versionBefore = mesh.version();
    const size_t dirtyBefore = mesh.dirtyCount();
    CHECK(mesh.rebuildDirty(1) == 1);
    CHECK(mesh.dirtyCount() == dirtyBefore - 1);
    mesh.rebuildDirty();
    CHECK(mesh.version() != versionBefore);
    const NavPath blocked = mesh.findPath(from, to);
    CHECK(!blocked.found || blocked.partial);
    CHECK(mesh.removeObstacle(blocker));
    mesh.rebuildDirty();
    const NavPath restored = mesh.findPath(from, to);
    CHECK(restored.found && !restored.partial && nearlyEqual(restored.length, direct.length, 0.01f));
    CHECK(mesh.hash() == again.hash());

    const uint32_t shortcut = mesh.addObstacle([] {
        Aabb b;
        b.grow(glm::vec3(-0.5f, 0.0f, 9.5f));
        b.grow(glm::vec3(0.5f, 2.0f, 20.5f));
        return b;
    }());
    mesh.rebuildDirty();
    mesh.moveObstacle(shortcut, [] {
        Aabb b;
        b.grow(glm::vec3(-0.5f, 0.0f, 15.0f));
        b.grow(glm::vec3(0.5f, 2.0f, 20.5f));
        return b;
    }());
    mesh.rebuildDirty();
    const NavPath moved = mesh.findPath(from, to);
    CHECK(moved.found && !moved.partial && nearlyEqual(moved.length, direct.length, 0.5f));

    World world;
    const auto floor = world.create("floor", NO_MESH, glm::vec3(0.0f, -0.5f, 0.0f));
    world.add<BoxCollider>(floor, glm::vec3(10.0f, 0.5f, 10.0f));
    NavMesh wm;
    wm.buildFromWorld(world);
    const NavPath wp = wm.findPath(glm::vec3(-6.0f, 0.0f, -6.0f), glm::vec3(6.0f, 0.0f, 6.0f));
    CHECK(wp.found && !wp.partial && nearlyEqual(wp.length, std::sqrt(2.0f) * 12.0f, 0.5f));
}

static void testNavLinksAndAgents() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, -0.5f, 0.0f)), glm::vec3(2.0f, 0.5f, 2.0f));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -0.5f, 0.0f)), glm::vec3(2.0f, 0.5f, 2.0f));
    NavMesh mesh;
    mesh.setGeometry(soup);
    mesh.rebuildAll();
    const glm::vec3 from(-3.5f, 0.0f, 0.0f);
    const glm::vec3 to(3.5f, 0.0f, 0.0f);
    const NavPath without = mesh.findPath(from, to);
    CHECK(!without.found || without.partial);

    NavLink link;
    link.a = glm::vec3(-1.8f, 0.0f, 0.0f);
    link.b = glm::vec3(1.8f, 0.0f, 0.0f);
    link.userData = 42;
    const uint32_t id = mesh.addLink(link);
    const NavPath with = mesh.findPath(from, to);
    CHECK(with.found && !with.partial);
    int linkPoints = 0;
    for (const NavPathPoint& p : with.points) linkPoints += p.link == static_cast<int>(id) ? 1 : 0;
    CHECK(linkPoints == 1);

    mesh.setLinkEnabled(id, false);
    const NavPath off = mesh.findPath(from, to);
    CHECK(!off.found || off.partial);
    mesh.setLinkEnabled(id, true);

    NavAgent agent;
    agent.position = from;
    navSetTarget(mesh, agent, to);
    CHECK(agent.state == AgentState::Moving);
    int traversals = 0;
    int userData = 0;
    for (int i = 0; i < 900 && agent.state == AgentState::Moving; ++i) {
        navStepAgent(mesh, agent, 1.0f / 30.0f, nullptr, [&](NavAgent& a, const NavLink& l, const glm::vec3& exit) {
            ++traversals;
            userData = l.userData;
            a.position = exit;
        });
    }
    CHECK(agent.state == AgentState::Arrived);
    CHECK(traversals == 1 && userData == 42);
    CHECK(glm::length(agent.position - to) < 0.3f);

    NavAgent crowdA;
    NavAgent crowdB;
    crowdA.position = glm::vec3(-3.5f, 0.0f, -1.0f);
    crowdB.position = glm::vec3(-3.5f, 0.0f, -0.9f);
    navSetTarget(mesh, crowdA, glm::vec3(-3.5f, 0.0f, 1.0f));
    navSetTarget(mesh, crowdB, glm::vec3(-3.5f, 0.0f, 1.0f));
    std::vector<NavAgent> crowd{crowdA, crowdB};
    for (int i = 0; i < 300; ++i) navStepAgents(mesh, crowd, 1.0f / 30.0f);
    CHECK(glm::length(crowd[0].position - glm::vec3(-3.5f, 0.0f, 1.0f)) < 1.0f && glm::length(crowd[1].position - glm::vec3(-3.5f, 0.0f, 1.0f)) < 1.0f);

    NavMesh floorMesh;
    floorMesh.setGeometry(floorWithWall());
    floorMesh.rebuildAll();
    NavAgent walker;
    walker.position = glm::vec3(-5.0f, 0.0f, 0.0f);
    navSetTarget(floorMesh, walker, glm::vec3(5.0f, 0.0f, 0.0f));
    for (int i = 0; i < 60; ++i) navStepAgent(floorMesh, walker, 1.0f / 30.0f);
    const uint32_t repathsBefore = walker.repaths;
    Aabb gap;
    gap.grow(glm::vec3(-0.5f, 0.0f, 9.5f));
    gap.grow(glm::vec3(0.5f, 2.0f, 20.5f));
    floorMesh.addObstacle(gap);
    floorMesh.rebuildDirty();
    for (int i = 0; i < 5; ++i) navStepAgent(floorMesh, walker, 1.0f / 30.0f);
    CHECK(walker.repaths > repathsBefore);
}

void runNavGeometryTests() {
    testGeometry();
    testNav();
    testNavLinksAndAgents();
}
