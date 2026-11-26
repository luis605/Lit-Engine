#include "TestSupport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Portal;
import Engine.Portal.view;
import Engine.glm;
import Engine.Save;
import Engine.Json;

static bool nearlyEqual(const glm::vec3& a, const glm::vec3& b, float eps = 1.0e-3f) { return glm::length(a - b) < eps; }

constexpr uint32_t kCube = 1;
constexpr uint32_t kSphere = 2;

static void setupWorld(World& w) {
    w.setMeshBoundsHook([](uint32_t id) { return id == kSphere ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) : glm::vec4(0.5f, 0.5f, 0.5f, 0.87f); });
}

static PortalSceneKit kit() {
    PortalSceneKit k;
    k.cube = kCube;
    k.sphere = kSphere;
    k.material = [](const glm::vec3&, const glm::vec3&, float, float) { return 0u; };
    return k;
}

static glm::vec3 forwardOf(const Camera& c) { return c.getFront(); }

static float matrixDifference(const glm::mat4& a, const glm::mat4& b) {
    float worst = 0.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) worst = std::max(worst, std::abs(a[column][row] - b[column][row]));
    }
    return worst;
}

static const char* kRayWorld = R"({
  "cells": [
    {"name": "X", "min": [-20, -1, -20], "max": [20, 6, 20]},
    {"name": "Y", "min": [80, -1, -20], "max": [120, 6, 130]}
  ],
  "portals": [
    {"name": "turn", "a": {"cell": "X", "position": [10, 1.5, 0], "rotation": [-90, 0, 0], "size": [1.5, 1.5]},
                     "b": {"cell": "Y", "position": [100, 1.5, 0], "rotation": [0, 0, 0], "size": [1.5, 1.5]}}
  ]
})";

static void testTransformAndLoop() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Loop);
    CHECK(p.cells().size() == 3);
    CHECK(p.portals().size() == 6);
    for (size_t i = 0; i < p.portals().size(); ++i) {
        const Portal& a = p.portals()[i];
        const Portal& b = p.portals()[static_cast<size_t>(a.partner)];
        CHECK(b.partner == static_cast<int>(i));
        const glm::vec3 front = a.center() + a.normal() * 0.7f + a.right() * 0.3f;
        const glm::vec3 mapped = glm::vec3(a.transform * glm::vec4(front, 1.0f));
        CHECK(nearlyEqual(mapped, b.center() - b.normal() * 0.7f - b.right() * 0.3f));
        CHECK(nearlyEqual(glm::vec3(a.transform * glm::vec4(a.normal(), 0.0f)), -b.normal()));
        CHECK(nearlyEqual(glm::vec3(b.transform * a.transform * glm::vec4(front, 1.0f)), front));
    }
    const glm::mat4 loop = p.portals()[4].transform * p.portals()[2].transform * p.portals()[0].transform;
    CHECK(glm::length(glm::vec3(loop[3])) > 1.0f);
    CHECK(!nearlyEqual(glm::vec3(loop * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 0.0f, 0.0f)));

    Camera cam;
    float yaw = 0.0f;
    float pitch = 0.0f;
    const glm::vec3 start = portalSceneStart(PortalTestScene::Loop, yaw, pitch);
    cam.setPos(start);
    cam.setOrientation(yaw, pitch);
    cam.setNearPlane(0.1f);
    p.setCameraCell(p.cellAt(start));
    CHECK(p.cameraCell() == p.findCell("A"));
    std::vector<int> visited;
    for (int i = 0; i < 2000 && visited.size() < 3; ++i) {
        cam.setPos(cam.getPosition() + forwardOf(cam) * 0.05f);
        const int crossed = p.step(cam);
        if (crossed >= 0) visited.push_back(p.cameraCell());
    }
    CHECK(visited.size() == 3);
    CHECK(visited.size() == 3 && visited[0] == p.findCell("B") && visited[1] == p.findCell("C") && visited[2] == p.findCell("A"));
    CHECK(p.cameraCell() == p.findCell("A"));
    CHECK(glm::length(cam.getPosition() - start) > 3.0f);
    CHECK(std::abs(std::fmod(cam.getYaw() - yaw + 360.0f, 360.0f) - 90.0f) < 0.5f);
    CHECK(p.cellAt(cam.getPosition()) == p.findCell("A"));
}

static void testObliqueClip() {
    Camera cam;
    cam.setPos(glm::vec3(0.0f, 0.0f, 5.0f));
    cam.setOrientation(-90.0f, 0.0f);
    cam.updateAspectRatio(16.0f, 9.0f);
    cam.setNearPlane(0.1f);
    cam.setFarPlane(100.0f);
    const glm::mat4 view = cam.getViewMatrix();
    const glm::mat4 proj = cam.getProjectionMatrix();
    const glm::vec4 plane(0.0f, 0.0f, 1.0f, -1.0f);
    const glm::mat4 oblique = obliqueProjection(proj, view, plane);
    auto ndcZ = [&](const glm::vec3& p) {
        const glm::vec4 c = oblique * view * glm::vec4(p, 1.0f);
        return c.z / c.w;
    };
    CHECK(std::abs(ndcZ(glm::vec3(0.3f, 0.2f, 1.0f)) - 1.0f) < 1.0e-4f);
    CHECK(std::abs(ndcZ(glm::vec3(-1.0f, 0.5f, 1.0f)) - 1.0f) < 1.0e-4f);
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, 0.0f)) < 1.0f);
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, 0.5f)) > ndcZ(glm::vec3(0.0f, 0.0f, -10.0f)));
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, 1.5f)) > 1.0f);
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, -50.0f)) > 0.0f);
}

static PortalFrame frameAtStart(PortalSystem& p, Camera& cam, float yaw = 0.0f) {
    float y = 0.0f;
    float pitch = 0.0f;
    const glm::vec3 start = portalSceneStart(PortalTestScene::Loop, y, pitch);
    cam.setPos(start);
    cam.setOrientation(yaw, pitch);
    cam.updateAspectRatio(16.0f, 9.0f);
    cam.setNearPlane(0.1f);
    cam.setFarPlane(500.0f);
    p.setCameraCell(p.cellAt(start));
    return p.buildFrame(cam, 1080.0f);
}

static void testFrameRecursion() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Loop);
    Camera cam;
    PortalFrame f = frameAtStart(p, cam);
    CHECK(f.nodes.size() == 4);
    int maxDepth = 0;
    for (const PortalViewNode& n : f.nodes) maxDepth = std::max(maxDepth, n.depth);
    CHECK(maxDepth == 3);
    CHECK(f.nodes[1].cell == p.findCell("B"));
    CHECK(f.nodes[2].cell == p.findCell("C"));
    CHECK(f.nodes[3].cell == p.findCell("A"));
    CHECK(f.nodes[0].surfaces.size() == 1 && f.nodes[0].surfaces[0].child == 1);
    CHECK(f.nodes[3].surfaces.empty() || f.nodes[3].surfaces[0].child == -1);

    p.settings().maxDepth = 1;
    f = frameAtStart(p, cam);
    CHECK(f.nodes.size() >= 2 && f.nodes.size() <= 3);
    CHECK(f.nodes.back().surfaces.size() == 1 && f.nodes.back().surfaces[0].child == -1);
    p.settings().maxDepth = 3;
    p.settings().maxViews = 3;
    f = frameAtStart(p, cam);
    CHECK(f.nodes.size() == 3);
    p.settings().maxViews = 24;

    f = frameAtStart(p, cam, 180.0f);
    CHECK(f.nodes.size() == 1);
    f = frameAtStart(p, cam, 0.0f);
    f = p.buildFrame(cam, 1080.0f, [](const glm::vec3&, const glm::vec3&) { return true; });
    CHECK(f.nodes.size() == 1);

    f = frameAtStart(p, cam);
    const PortalViewNode& through = f.nodes[1];
    const Portal& dst = p.portals()[static_cast<size_t>(p.portals()[0].partner)];
    CHECK(glm::dot(glm::vec3(through.planes[4]), dst.center()) + through.planes[4].w < 1.0e-3f);
    const glm::vec3 inside = dst.center() + dst.normal() * 3.0f;
    const glm::vec3 beside = dst.center() + dst.normal() * 3.0f + dst.right() * 4.0f;
    auto accepts = [&](const glm::vec3& pt) {
        bool ok = true;
        for (const glm::vec4& pl : through.planes) ok = ok && glm::dot(glm::vec3(pl), pt) + pl.w >= 0.0f;
        return ok;
    };
    CHECK(accepts(inside));
    CHECK(!accepts(beside));
    CHECK(!accepts(dst.center() - dst.normal() * 1.0f));
}

static void testCrossingContinuity() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Loop);
    Camera cam;
    cam.updateAspectRatio(16.0f, 9.0f);
    cam.setNearPlane(0.1f);
    cam.setFarPlane(500.0f);
    cam.setPos(glm::vec3(3.0f, 1.7f, 0.3f));
    cam.setOrientation(8.0f, -2.0f);
    p.setCameraCell(p.findCell("A"));
    const glm::vec3 target(24.0f, 1.5f, 0.5f);
    auto viewOf = [&]() {
        const PortalFrame f = p.buildFrame(cam, 1080.0f);
        const int idx = p.cameraCell() == p.findCell("A") ? 1 : 0;
        return glm::vec3(f.nodes[static_cast<size_t>(idx)].view * glm::vec4(target, 1.0f));
    };
    glm::vec3 previous = viewOf();
    float worstJump = 0.0f;
    bool crossed = false;
    for (int i = 0; i < 300; ++i) {
        cam.setPos(cam.getPosition() + forwardOf(cam) * 0.02f);
        if (p.step(cam) >= 0) crossed = true;
        const glm::vec3 now = viewOf();
        worstJump = std::max(worstJump, glm::length(now - previous));
        previous = now;
    }
    CHECK(crossed);
    CHECK(p.cameraCell() == p.findCell("B"));
    CHECK(worstJump < 0.05f);
}

static void testRaycastAndOverlap() {
    World w;
    setupWorld(w);
    PortalSystem p;
    CHECK(p.parseJson(kRayWorld));
    const int x = p.findCell("X");
    const Portal& a = p.portals()[0];
    const glm::vec3 origin(0.0f, 1.5f, 0.2f);
    const glm::vec3 cross(10.0f, 1.5f, 0.2f);
    const glm::vec3 emerge = glm::vec3(a.transform * glm::vec4(cross, 1.0f));
    const glm::vec3 outDir = glm::vec3(a.transform * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    auto sphere = [&](const char* name, const glm::vec3& at, float scale) {
        EntityDesc d;
        d.name = name;
        d.mesh = kSphere;
        d.position = at;
        d.scale = glm::vec3(scale);
        return w.create(d);
    };
    const EntityHandle target = sphere("target", emerge + outDir * 6.0f, 0.5f);

    const PortalRayHit through = p.raycast(w, origin, glm::vec3(1.0f, 0.0f, 0.0f), 100.0f, x);
    CHECK(through.crossings == 1);
    CHECK(through.hit && through.hit->entity == target);
    CHECK(through.hit && std::abs(through.distance - (10.0f + 5.5f)) < 0.02f);
    CHECK(through.cell == p.findCell("Y"));
    CHECK(nearlyEqual(through.direction, outDir));
    CHECK(nearlyEqual(through.point, emerge + outDir * 5.5f, 0.02f));

    const PortalRayHit away = p.raycast(w, origin, glm::vec3(-1.0f, 0.0f, 0.0f), 100.0f, x);
    CHECK(!away.hit && away.crossings == 0);
    const PortalRayHit miss = p.raycast(w, origin, glm::vec3(1.0f, 0.0f, 0.0f), 8.0f, x);
    CHECK(!miss.hit && miss.crossings == 0);
    const PortalRayHit aside = p.raycast(w, glm::vec3(0.0f, 1.5f, 5.0f), glm::vec3(1.0f, 0.0f, 0.0f), 100.0f, x);
    CHECK(!aside.hit && aside.crossings == 0);
    const PortalRayHit short1 = p.raycast(w, origin, glm::vec3(1.0f, 0.0f, 0.0f), 12.0f, x);
    CHECK(!short1.hit && short1.crossings == 1 && std::abs(short1.distance - 12.0f) < 0.02f);

    const EntityHandle blocker = sphere("blocker", glm::vec3(7.0f, 1.5f, 0.2f), 0.5f);
    const PortalRayHit blocked = p.raycast(w, origin, glm::vec3(1.0f, 0.0f, 0.0f), 100.0f, x);
    CHECK(blocked.crossings == 0 && blocked.hit && blocked.hit->entity == blocker);

    const EntityHandle beyond = sphere("beyond", glm::vec3(100.3f, 1.5f, -0.6f), 0.4f);
    const auto overlaps = p.overlapSphere(w, glm::vec3(9.5f, 1.5f, 0.0f), 1.2f, x);
    const auto found = std::find_if(overlaps.begin(), overlaps.end(), [&](const PortalOverlap& o) { return o.entity == beyond; });
    CHECK(found != overlaps.end() && found->crossings == 1);
    CHECK(std::none_of(overlaps.begin(), overlaps.end(), [&](const PortalOverlap& o) { return o.entity == target; }));
    const auto direct = p.overlapSphere(w, glm::vec3(9.5f, 1.5f, 0.0f), 0.2f, x);
    CHECK(std::none_of(direct.begin(), direct.end(), [&](const PortalOverlap& o) { return o.entity == beyond; }));
    const auto local = p.overlapSphere(w, glm::vec3(7.0f, 1.5f, 0.0f), 1.0f, x);
    CHECK(std::any_of(local.begin(), local.end(), [&](const PortalOverlap& o) { return o.entity == blocker && o.crossings == 0; }));
}

static void testStraddlingObjects() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Loop);
    const EntityHandle probe = w.find("Probe");
    CHECK(w.isAlive(probe));
    p.updateObjects(w);
    const EntityHandle ghost = p.ghostOf(probe);
    CHECK(w.isAlive(ghost));
    CHECK(w.isVisible(ghost));
    CHECK(p.isGhost(ghost));
    const Portal& ab = p.portals()[0];
    CHECK(nearlyEqual(w.getWorldPosition(ghost), glm::vec3(ab.transform * glm::vec4(w.getWorldPosition(probe), 1.0f))));
    CHECK(w.getMesh(ghost) == w.getMesh(probe));
    CHECK(p.objectCell(probe) == p.findCell("A"));

    w.setWorldPosition(probe, glm::vec3(0.0f, 1.2f, 0.4f));
    p.updateObjects(w);
    CHECK(!w.isVisible(ghost));

    w.setWorldPosition(probe, glm::vec3(4.6f, 1.2f, 0.4f));
    p.updateObjects(w);
    CHECK(w.isVisible(ghost));
    w.setWorldPosition(probe, glm::vec3(5.2f, 1.2f, 0.4f));
    p.updateObjects(w);
    CHECK(p.objectCell(probe) == p.findCell("B"));
    CHECK(nearlyEqual(w.getWorldPosition(probe), glm::vec3(19.2f, 1.2f, 0.4f)));
    CHECK(w.isVisible(ghost));
    CHECK(nearlyEqual(w.getWorldPosition(ghost), glm::vec3(5.2f, 1.2f, 0.4f), 0.05f));
    p.untrack(w, probe);
    CHECK(!w.isAlive(ghost));
}

static const char* kBlendWorld = R"({
  "settings": {"maxDepth": 2, "maxViews": 8, "blendDistance": 2.0},
  "cells": [
    {"name": "X", "min": [-20, -1, -20], "max": [20, 6, 20], "gravity": [0, -9.81, 0], "tint": [1, 0, 0], "sun": 1.0, "ambient": 1.0, "scalars": {"drag": 0.0}},
    {"name": "Y", "min": [80, -1, -20], "max": [120, 6, 20], "gravity": [-9.81, 0, 0], "tint": [0, 0, 1], "sun": 0.0, "ambient": 0.5, "scalars": {"drag": 2.0}}
  ],
  "portals": [
    {"name": "twist", "a": {"cell": "X", "position": [10, 1.5, 0], "rotation": [-90, 0, 0], "size": [1.5, 1.5]},
                       "b": {"cell": "Y", "position": [100, 1.5, 0], "rotation": [0, 0, 0], "size": [1.5, 1.5]}}
  ]
})";

static void testCellParamBlend() {
    PortalSystem p;
    CHECK(p.parseJson(kBlendWorld));
    CHECK(p.cells().size() == 2 && p.portals().size() == 2);
    CHECK(p.settings().maxDepth == 2 && p.settings().maxViews == 8);
    const int x = p.findCell("X");
    const int y = p.findCell("Y");
    const PortalCellParams far = p.sampleParams(x, glm::vec3(-10.0f, 1.0f, 0.0f));
    CHECK(nearlyEqual(far.gravity, glm::vec3(0.0f, -9.81f, 0.0f), 1.0e-5f) && far.scalars.at("drag") == 0.0f);
    const PortalCellParams mid = p.sampleParams(x, glm::vec3(10.0f, 1.5f, 0.0f));
    const glm::mat4 inv = glm::inverse(p.portals()[0].transform);
    const glm::vec3 other = glm::vec3(inv * glm::vec4(-9.81f, 0.0f, 0.0f, 0.0f));
    CHECK(nearlyEqual(mid.gravity, 0.5f * (glm::vec3(0.0f, -9.81f, 0.0f) + other), 1.0e-3f));
    CHECK(std::abs(mid.scalars.at("drag") - 1.0f) < 1.0e-4f && std::abs(mid.sun - 0.5f) < 1.0e-4f);
    const PortalCellParams quarter = p.sampleParams(x, glm::vec3(9.0f, 1.5f, 0.0f));
    CHECK(quarter.scalars.at("drag") > 0.0f && quarter.scalars.at("drag") < 1.0f);

    float worst = 0.0f;
    glm::vec3 previous(0.0f);
    bool havePrevious = false;
    glm::vec3 position(5.0f, 1.5f, 0.0f);
    glm::vec3 dir(1.0f, 0.0f, 0.0f);
    int cell = x;
    bool crossed = false;
    for (int i = 0; i < 900; ++i) {
        glm::vec3 next = position + dir * 0.02f;
        const int id = p.stepPoint(next, cell, position);
        if (id >= 0) {
            crossed = true;
            dir = glm::normalize(glm::vec3(p.portals()[static_cast<size_t>(id)].transform * glm::vec4(dir, 0.0f)));
        }
        position = next;
        const glm::vec3 g = p.sampleParams(cell, position).gravity;
        const glm::vec3 common = cell == x ? g : glm::vec3(inv * glm::vec4(g, 0.0f));
        if (havePrevious) worst = std::max(worst, glm::length(common - previous));
        previous = common;
        havePrevious = true;
    }
    CHECK(crossed);
    CHECK(cell == y);
    CHECK(worst < 0.2f);
    CHECK(nearlyEqual(p.sampleParams(y, glm::vec3(110.0f, 1.0f, 0.0f)).gravity, glm::vec3(-9.81f, 0.0f, 0.0f), 1.0e-5f));
}

static void testJson() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Torture);
    CHECK(p.cells().size() == 2 && p.portals().size() == 8);
    const std::string text = p.toJson();
    PortalSystem q;
    CHECK(q.parseJson(text));
    CHECK(q.cells().size() == p.cells().size() && q.portals().size() == p.portals().size());
    for (size_t i = 0; i < p.portals().size(); ++i) {
        CHECK(nearlyEqual(q.portals()[i].center(), p.portals()[i].center(), 1.0e-2f));
        CHECK(nearlyEqual(q.portals()[i].normal(), p.portals()[i].normal(), 1.0e-2f));
        CHECK(glm::length(glm::vec3(q.portals()[i].transform[3]) - glm::vec3(p.portals()[i].transform[3])) < 0.05f);
    }
    PortalSystem bad;
    CHECK(!bad.parseJson("{"));
    CHECK(!bad.parseJson("[]"));
    CHECK(!bad.parseJson("{\"cells\": []}"));
    CHECK(!bad.parseJson("{\"cells\": [{\"name\": \"A\"}], \"portals\": [{\"a\": {\"cell\": \"A\"}, \"b\": {\"cell\": \"Z\"}}]}"));
    CHECK(!bad.parseJson("{\"cells\": [{\"name\": \"A\"}, {\"name\": \"A\"}]}"));
    CHECK(!bad.active());

    PortalFrame frame;
    Camera cam;
    cam.updateAspectRatio(16.0f, 9.0f);
    cam.setFarPlane(300.0f);
    float yaw = 0.0f;
    float pitch = 0.0f;
    cam.setPos(portalSceneStart(PortalTestScene::Torture, yaw, pitch));
    cam.setOrientation(yaw, pitch);
    p.setCameraCell(p.findCell("T"));
    p.settings().maxViews = 32;
    p.settings().maxDepth = 6;
    frame = p.buildFrame(cam, 1080.0f);
    CHECK(frame.nodes.size() > 4 && frame.nodes.size() <= 32);
    for (const PortalViewNode& node : frame.nodes) {
        for (const PortalSurface& s : node.surfaces) CHECK(s.child < static_cast<int>(frame.nodes.size()));
    }
}

static void testPhysicalWalkBetweenCells() {
    PortalSystem p;
    auto cell = [&](const char* name, float x0, float x1) {
        PortalCell c;
        c.name = name;
        c.min = glm::vec3(x0, -0.4f, -6.0f);
        c.max = glm::vec3(x1, 4.0f, 6.0f);
        p.addCell(c);
    };
    cell("A", -5.0f, 6.0f);
    cell("B", 14.0f, 30.0f);
    cell("C", 30.0f, 46.0f);
    auto end = [](const char* name, const glm::vec3& pos, float yaw) {
        PortalEnd e;
        e.cell = name;
        e.position = pos;
        e.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
        e.halfSize = glm::vec2(1.0f, 1.25f);
        return e;
    };
    p.addPair("AB", end("A", glm::vec3(5.0f, 1.25f, 0.0f), -90.0f), end("B", glm::vec3(19.0f, 1.25f, 0.0f), 90.0f));
    p.addPair("CA", end("C", glm::vec3(43.0f, 1.25f, 0.0f), -90.0f), end("A", glm::vec3(0.0f, 1.25f, -5.0f), 0.0f));
    Camera cam;
    cam.updateAspectRatio(16.0f, 9.0f);
    cam.setNearPlane(0.1f);
    cam.setFarPlane(200.0f);
    cam.setPos(glm::vec3(2.0f, 1.7f, 0.0f));
    cam.setOrientation(0.0f, 0.0f);
    p.setCameraCell(p.findCell("A"));
    bool crossed = false;
    for (int i = 0; i < 2000 && p.cameraCell() != p.findCell("C"); ++i) {
        cam.setPos(cam.getPosition() + forwardOf(cam) * 0.05f);
        if (p.step(cam) >= 0) crossed = true;
    }
    CHECK(crossed);
    CHECK(p.cameraCell() == p.findCell("C"));
    const PortalFrame f = p.buildFrame(cam, 1080.0f);
    CHECK(!f.nodes.empty() && f.nodes[0].cell == p.findCell("C"));
}

static void testPortalPresentUntilCrossing() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Loop);
    const Portal& ab = p.portals()[0];
    const glm::vec3 target = ab.center();
    const float yaws[] = {180.0f, 170.0f, 195.0f, 155.0f, 205.0f};
    const float pitches[] = {0.0f, -8.0f, 6.0f};
    int missing = 0;
    int crossings = 0;
    for (float yawOffset : yaws) {
        for (float pitch : pitches) {
            Camera cam;
            cam.updateAspectRatio(16.0f, 9.0f);
            cam.setNearPlane(0.1f);
            cam.setFarPlane(500.0f);
            cam.setOrientation(yawOffset - 180.0f, pitch);
            const glm::vec3 fwd = forwardOf(cam);
            cam.setPos(target - fwd * 3.0f);
            p.setCameraCell(p.findCell("A"));
            for (int i = 0; i < 400; ++i) {
                cam.setPos(cam.getPosition() + fwd * 0.01f);
                if (p.step(cam) >= 0) {
                    ++crossings;
                    break;
                }
                const PortalFrame f = p.buildFrame(cam, 1080.0f);
                if (f.nodes.empty() || f.nodes[0].surfaces.empty()) ++missing;
            }
        }
    }
    CHECK(crossings == 15);
    CHECK(missing == 0);
}

static void testLightTransport() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Loop);
    const int a = p.findCell("A");
    const int b = p.findCell("B");
    const int c = p.findCell("C");
    const glm::vec3 lamp(24.0f, 3.6f, 0.0f);
    const std::vector<PortalLightImage> images = p.lightImages(b, lamp, 16.0f);
    CHECK(images.size() == 2);
    bool sawA = false;
    bool sawC = false;
    for (const PortalLightImage& image : images) {
        const Portal& receiver = p.portals()[static_cast<size_t>(image.portal)];
        const Portal& source = p.portals()[static_cast<size_t>(receiver.partner)];
        CHECK(image.cell == receiver.cell);
        CHECK(source.cell == b);
        const glm::vec3 position(image.transform * glm::vec4(lamp, 1.0f));
        CHECK(glm::dot(image.normal, position - image.center) < 0.0f);
        CHECK(std::abs(glm::distance(position, image.center) - glm::distance(lamp, source.center())) < 1.0e-3f);
        CHECK(nearlyEqual(image.normal, receiver.normal()));
        CHECK(nearlyEqual(image.axisU, receiver.right() * receiver.half.x));
        sawA = sawA || image.cell == a;
        sawC = sawC || image.cell == c;
    }
    CHECK(sawA && sawC);
    CHECK(p.lightImages(b, lamp, 2.0f).empty());
    CHECK(p.lightImages(-1, lamp, 16.0f).empty());
    p.settings().lightTransport = false;
    CHECK(p.lightImages(b, lamp, 16.0f).empty());
    p.settings().lightTransport = true;

    const glm::vec3 tintA = p.lightTint(a);
    CHECK(std::abs(tintA.x - 1.0f) < 1.0e-4f && tintA.y < 0.9f && tintA.z < tintA.y);
    p.cellParams(a).scalars["tintLighting"] = 0.0f;
    CHECK(nearlyEqual(p.lightTint(a), glm::vec3(1.0f)));
    p.cellParams(a).scalars.erase("tintLighting");
    PortalCell grey;
    grey.name = "grey";
    grey.min = glm::vec3(200.0f);
    grey.max = glm::vec3(210.0f);
    const int greyCell = p.addCell(grey);
    CHECK(nearlyEqual(p.lightTint(greyCell), glm::vec3(1.0f)));

    Camera cam;
    cam.setPos(glm::vec3(-3.2f, 1.7f, 0.0f));
    cam.setOrientation(0.0f, 0.0f);
    p.setCameraCell(a);
    const PortalFrame frame = p.buildFrame(cam, 720.0f);
    CHECK(frame.nodes.size() >= 2);
    for (const PortalViewNode& node : frame.nodes) {
        CHECK(nearlyEqual(node.lightTint, p.lightTint(node.cell)));
        CHECK(nearlyEqual(node.cellMin, p.cells()[static_cast<size_t>(node.cell)].min) && nearlyEqual(node.cellMax, p.cells()[static_cast<size_t>(node.cell)].max));
    }

    p.settings().tintLighting = 0.5f;
    p.settings().lightTransport = false;
    PortalSystem copy;
    CHECK(copy.parseJson(p.toJson()));
    CHECK(std::abs(copy.settings().tintLighting - 0.5f) < 1.0e-6f && !copy.settings().lightTransport);
}

static void testCameraOrientation() {
    Camera cam;
    cam.setPos(glm::vec3(1.0f, 2.0f, 3.0f));
    cam.setOrientation(30.0f, -20.0f, 35.0f);
    const glm::vec3 front = cam.getFront();
    Camera upright;
    upright.setPos(cam.getPosition());
    upright.setOrientation(30.0f, -20.0f);
    CHECK(nearlyEqual(front, upright.getFront(), 1.0e-5f));
    CHECK(std::abs(glm::dot(cam.getUp(), upright.getUp()) - std::cos(glm::radians(35.0f))) < 1.0e-4f);
    CHECK(std::abs(glm::dot(cam.getUp(), front)) < 1.0e-5f);
    Camera copy;
    copy.setPos(cam.getPosition());
    copy.setRotation(cam.getRotation());
    CHECK(matrixDifference(copy.getViewMatrix(), cam.getViewMatrix()) < 1.0e-4f);
    CHECK(std::abs(copy.getRoll() - 35.0f) < 1.0e-2f);
    for (float pitch : {90.0f, -90.0f}) {
        Camera vertical;
        vertical.setOrientation(70.0f, pitch, 0.0f);
        Camera decoded;
        decoded.setRotation(vertical.getRotation());
        CHECK(matrixDifference(decoded.getViewMatrix(), vertical.getViewMatrix()) < 1.0e-4f);
        CHECK(std::isfinite(decoded.getYaw()) && std::abs(std::abs(decoded.getPitch()) - 90.0f) < 1.0e-3f);
    }
    Camera turned;
    turned.setOrientation(10.0f, 5.0f);
    turned.transformBy(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(!turned.hasFrame() && turned.getRoll() == 0.0f);
    CHECK(std::abs(std::fmod(turned.getYaw() - 10.0f + 720.0f, 360.0f) - 270.0f) < 1.0e-3f);
}

static void testTiltedCrossings() {
    World w;
    setupWorld(w);
    PortalSystem p;
    buildPortalScene(p, w, kit(), PortalTestScene::Tilt);
    CHECK(p.cells().size() == 5);
    CHECK(p.portals().size() == 8);
    const char* names[] = {"floor.a", "ceiling.a", "rolled.a", "flip.a", "floor.b", "rolled.b", "flip.b"};
    int crossings = 0;
    float worstSwap = 0.0f;
    float worstChild = 0.0f;
    for (const char* name : names) {
        int id = -1;
        for (size_t i = 0; i < p.portals().size(); ++i) {
            if (p.portals()[i].name == name) id = static_cast<int>(i);
        }
        CHECK(id >= 0);
        if (id < 0) continue;
        const Portal& portal = p.portals()[static_cast<size_t>(id)];
        const Portal& exit = p.portals()[static_cast<size_t>(portal.partner)];
        for (const glm::vec2 offset : {glm::vec2(0.0f), glm::vec2(0.3f, -0.2f), glm::vec2(-0.4f, 0.35f)}) {
            for (const float roll : {0.0f, 25.0f}) {
                Camera cam;
                cam.updateAspectRatio(16.0f, 9.0f);
                cam.setNearPlane(0.05f);
                cam.setFarPlane(300.0f);
                const glm::vec3 target = portal.center() + portal.right() * offset.x + portal.up() * offset.y;
                const glm::vec3 start = target + portal.normal() * 1.5f + portal.right() * 0.4f;
                const glm::vec3 dir = glm::normalize(target - start);
                glm::mat3 look(1.0f);
                const glm::vec3 side = glm::normalize(glm::cross(dir, std::abs(dir.y) > 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f)));
                look = glm::mat3(side, glm::cross(side, dir), -dir);
                cam.setPos(start);
                cam.setRotation(look);
                cam.setOrientation(cam.getYaw(), cam.getPitch(), roll);
                p.setCameraCell(portal.cell);
                bool crossed = false;
                for (int step = 0; step < 400 && !crossed; ++step) {
                    const Camera before = cam;
                    cam.setPos(cam.getPosition() + cam.getFront() * 0.013f);
                    const Camera moved = cam;
                    const PortalFrame frame = p.buildFrame(moved, 1080.0f);
                    const int crossedId = p.step(cam);
                    if (crossedId < 0) continue;
                    crossed = true;
                    ++crossings;
                    CHECK(crossedId == id);
                    CHECK(p.cameraCell() == exit.cell);
                    worstSwap = std::max(worstSwap, matrixDifference(cam.getViewMatrix(), moved.getViewMatrix() * glm::inverse(portal.transform)));
                    for (const PortalSurface& surface : frame.nodes.empty() ? std::vector<PortalSurface>{} : frame.nodes[0].surfaces) {
                        if (surface.child > 0 && nearlyEqual(surface.center, portal.center(), 1.0e-4f)) worstChild = std::max(worstChild, matrixDifference(cam.getViewMatrix(), frame.nodes[static_cast<size_t>(surface.child)].view));
                    }
                    (void)before;
                    const glm::mat4 crossedView = cam.getViewMatrix();
                    cam.relaxFrame(p.cellUp(p.cameraCell()), 0.0f, 0.3f);
                    CHECK(matrixDifference(cam.getViewMatrix(), crossedView) < 2.0e-4f);
                    for (int tick = 0; tick < 60; ++tick) cam.relaxFrame(p.cellUp(p.cameraCell()), 1.0f / 60.0f, 0.3f);
                    CHECK(!cam.relaxing());
                    const bool upright = glm::vec3(portal.transform * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)).y > 0.9999f;
                    CHECK(std::abs(cam.getRoll() - (upright ? roll : 0.0f)) < 1.0e-3f && std::abs(cam.getPitch()) <= 89.0f + 1.0e-3f);
                    if (!upright) CHECK(cam.getUp().y > 0.0f && glm::length(glm::cross(cam.getRight(), glm::vec3(0.0f, 1.0f, 0.0f))) > 0.999f - 1.0e-3f);
                    CHECK(!cam.hasFrame());
                }
                CHECK(crossed);
            }
        }
    }
    CHECK(crossings == 7 * 3 * 2);
    CHECK(worstSwap < 5.0e-4f);
    CHECK(worstChild < 5.0e-4f);
}

static void testFlipUprightImmediate() {
    Camera cam;
    cam.setOrientation(-30.0f, 10.0f);
    const glm::mat4 flip = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    cam.transformBy(flip);
    CHECK(cam.hasFrame());
    CHECK(cam.getUp().y < -0.9f);
    const glm::vec3 front = cam.getFront();
    cam.relaxFrame(glm::vec3(0.0f, 1.0f, 0.0f), 1.0f / 60.0f, 0.0f);
    CHECK(nearlyEqual(cam.getFront(), front, 1.0e-4f));
    CHECK(cam.getUp().y > 0.9f && !cam.relaxing() && cam.getRoll() == 0.0f);
}

static void testOrientationPersistence() {
    World w;
    setupWorld(w);
    Camera& cam = w.camera();
    cam.setPos(glm::vec3(1.0f, 2.0f, 3.0f));
    cam.setOrientation(20.0f, -10.0f, 0.0f);
    cam.transformBy(glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
    cam.setOrientation(cam.getYaw(), cam.getPitch(), 12.0f);
    const glm::mat4 view = cam.getViewMatrix();
    std::ostringstream out;
    CHECK(w.saveScene(out));
    World loaded;
    setupWorld(loaded);
    std::istringstream in(out.str());
    CHECK(loaded.loadScene(in));
    CHECK(matrixDifference(loaded.camera().getViewMatrix(), view) < 1.0e-4f);
    CHECK(std::abs(loaded.camera().getRoll() - 12.0f) < 1.0e-3f && loaded.camera().hasFrame());

    SaveContext context;
    context.world = &w;
    SaveSystem saves(context);
    const Json saved = saves.capture("tilt");
    World restored;
    setupWorld(restored);
    SaveContext restoredContext;
    restoredContext.world = &restored;
    SaveSystem restoredSaves(restoredContext);
    std::string error;
    CHECK(restoredSaves.apply(saved, &error));
    CHECK(matrixDifference(restored.camera().getViewMatrix(), view) < 1.0e-4f);

    World legacy;
    setupWorld(legacy);
    legacy.camera().transformBy(glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    std::istringstream old("LITSCENE 3\n0\ncamera 1 2 3 -90 10 45 0.1 100\n");
    CHECK(legacy.loadScene(old));
    CHECK(legacy.camera().getRoll() == 0.0f && !legacy.camera().hasFrame());
    CHECK(std::abs(legacy.camera().getPitch() - 10.0f) < 1.0e-4f);
    CHECK(std::abs(legacy.camera().getUp().x) < 1.0e-4f);
}

static void testShapesAndLook() {
    PortalSystem p;
    CHECK(p.parseJson(R"({
  "settings": {"uprightSeconds": 0.5, "uprightCamera": false},
  "cells": [{"name": "X", "min": [-20, -10, -20], "max": [20, 10, 20]}, {"name": "Y", "min": [80, -10, -20], "max": [120, 10, 20]}],
  "portals": [{"name": "oval", "shape": "ellipse", "spill": 0.5,
               "frame": {"width": 0.2, "depth": 0.4, "color": [0.1, 0.2, 0.3], "metalness": 0.5, "emissive": [1, 0, 0], "emissiveIntensity": 100},
               "edge": {"feather": 2, "rim": 0.7, "rimColor": [0, 1, 0], "distortion": 4, "chroma": 2, "dissolve": 0.25},
               "a": {"cell": "X", "position": [0, 0, 0], "rotation": [0, -90, 30], "size": [1.5, 1.0]},
               "b": {"cell": "Y", "position": [100, 0, 0], "rotation": [0, 90, 0], "size": [1.5, 1.0]}}]
})"));
    CHECK(std::abs(p.settings().uprightSeconds - 0.5f) < 1.0e-6f && !p.settings().uprightCamera);
    const Portal& a = p.portals()[0];
    CHECK(a.look.shape == PortalShape::Ellipse);
    CHECK(std::abs(a.look.frame.width - 0.2f) < 1.0e-6f && std::abs(a.look.frame.depth - 0.4f) < 1.0e-6f);
    CHECK(std::abs(a.look.edge.dissolve - 0.25f) < 1.0e-6f && std::abs(a.look.spill - 0.5f) < 1.0e-6f);
    CHECK(nearlyEqual(a.normal(), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(a.inside(0.0f, 0.0f) && a.inside(1.4f, 0.0f) && !a.inside(1.4f, 0.9f));
    World w;
    const glm::vec3 corner = a.center() + a.right() * 1.4f + a.up() * 0.9f;
    CHECK(p.raycast(w, corner + glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 0).crossings == 0);
    CHECK(p.raycast(w, a.center() + glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 0).crossings == 1);
    CHECK(portalShapeDistance(PortalShape::Rounded, glm::vec2(1.0f), 0.5f, 0.95f, 0.95f) > 0.0f);
    CHECK(portalShapeDistance(PortalShape::Rect, glm::vec2(1.0f), 0.5f, 0.95f, 0.95f) < 0.0f);
    PortalSystem copy;
    CHECK(copy.parseJson(p.toJson()));
    const Portal& c = copy.portals()[0];
    CHECK(c.look.shape == PortalShape::Ellipse && std::abs(c.look.edge.rim - 0.7f) < 1.0e-6f && nearlyEqual(c.look.frame.emissive, glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(matrixDifference(c.pose, a.pose) < 1.0e-5f);
    CHECK(nearlyEqual(c.euler, glm::vec3(0.0f, -90.0f, 30.0f)));
    for (PortalShape shape : {PortalShape::Rect, PortalShape::Ellipse, PortalShape::Rounded}) {
        PortalLook look;
        look.shape = shape;
        PortalFrameMesh body;
        PortalFrameMesh reveal;
        buildPortalFrameMesh(glm::vec2(1.1f, 1.6f), look, body, reveal);
        CHECK(!body.indices.empty() && !reveal.indices.empty());
        CHECK(body.vertices.size() % 8 == 0 && reveal.vertices.size() % 8 == 0);
        bool finite = true;
        for (float v : body.vertices) finite = finite && std::isfinite(v);
        for (unsigned int index : body.indices) finite = finite && index < body.vertices.size() / 8;
        float minZ = 1.0e9f;
        float maxZ = -1.0e9f;
        for (size_t v = 0; v < reveal.vertices.size(); v += 8) {
            minZ = std::min(minZ, reveal.vertices[v + 2]);
            maxZ = std::max(maxZ, reveal.vertices[v + 2]);
            finite = finite && std::abs(portalShapeDistance(shape, glm::vec2(1.1f, 1.6f), look.cornerRadius, reveal.vertices[v], reveal.vertices[v + 1])) < 1.0e-3f;
        }
        CHECK(finite);
        CHECK(minZ < -0.05f && maxZ > 0.2f);
    }
    std::vector<glm::vec4> packed = {glm::vec4(100.0f, 2.0f, 0.0f, 10.0f), glm::vec4(1.0f, 0.5f, 0.25f, 1000.0f), glm::vec4(0.0f), glm::vec4(0.0f)};
    const std::vector<int32_t> cells = {copy.findCell("Y")};
    const glm::vec3 spill = copy.spillIrradiance(0, packed, cells, glm::vec3(0.0f), glm::vec3(0.0f));
    CHECK(spill.x > spill.y && spill.y > spill.z && spill.z > 0.0f);
    PortalLook off = copy.portals()[0].look;
    off.spill = 0.0f;
    copy.setLook(0, off);
    CHECK(copy.spillIrradiance(0, packed, cells, glm::vec3(0.0f), glm::vec3(0.0f)) == glm::vec3(0.0f));
}

int main() {
    testTransformAndLoop();
    testObliqueClip();
    testFrameRecursion();
    testCrossingContinuity();
    testRaycastAndOverlap();
    testStraddlingObjects();
    testCellParamBlend();
    testJson();
    testPhysicalWalkBetweenCells();
    testPortalPresentUntilCrossing();
    testLightTransport();
    testCameraOrientation();
    testTiltedCrossings();
    testFlipUprightImmediate();
    testOrientationPersistence();
    testShapesAndLook();
    return testsupport::finish();
}
