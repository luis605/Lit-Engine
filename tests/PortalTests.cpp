#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Portal;
import Engine.Portal.view;
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

static bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1.0e-3f) { return glm::length(a - b) < eps; }

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

static glm::vec3 forwardOf(const Camera& c) {
    const float yaw = glm::radians(c.getYaw());
    const float pitch = glm::radians(c.getPitch());
    return glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
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
        CHECK(near(mapped, b.center() - b.normal() * 0.7f - b.right() * 0.3f));
        CHECK(near(glm::vec3(a.transform * glm::vec4(a.normal(), 0.0f)), -b.normal()));
        CHECK(near(glm::vec3(b.transform * a.transform * glm::vec4(front, 1.0f)), front));
    }
    const glm::mat4 loop = p.portals()[4].transform * p.portals()[2].transform * p.portals()[0].transform;
    CHECK(glm::length(glm::vec3(loop[3])) > 1.0f);
    CHECK(!near(glm::vec3(loop * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 0.0f, 0.0f)));

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
    CHECK(std::abs(ndcZ(glm::vec3(0.3f, 0.2f, 1.0f))) < 1.0e-4f);
    CHECK(std::abs(ndcZ(glm::vec3(-1.0f, 0.5f, 1.0f))) < 1.0e-4f);
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, 0.0f)) > 0.0f);
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, 0.5f)) < ndcZ(glm::vec3(0.0f, 0.0f, -10.0f)));
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, 1.5f)) < 0.0f);
    CHECK(ndcZ(glm::vec3(0.0f, 0.0f, -50.0f)) < 1.0f);
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
    CHECK(f.nodes.size() == 2);
    CHECK(f.nodes[1].surfaces.size() == 1 && f.nodes[1].surfaces[0].child == -1);
    p.settings().maxDepth = 3;
    p.settings().maxViews = 3;
    f = frameAtStart(p, cam);
    CHECK(f.nodes.size() == 3);
    p.settings().maxViews = 16;

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
    CHECK(near(through.direction, outDir));
    CHECK(near(through.point, emerge + outDir * 5.5f, 0.02f));

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
    CHECK(near(w.getWorldPosition(ghost), glm::vec3(ab.transform * glm::vec4(w.getWorldPosition(probe), 1.0f))));
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
    CHECK(near(w.getWorldPosition(probe), glm::vec3(19.2f, 1.2f, 0.4f)));
    CHECK(w.isVisible(ghost));
    CHECK(near(w.getWorldPosition(ghost), glm::vec3(5.2f, 1.2f, 0.4f), 0.05f));
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
    CHECK(near(far.gravity, glm::vec3(0.0f, -9.81f, 0.0f), 1.0e-5f) && far.scalars.at("drag") == 0.0f);
    const PortalCellParams mid = p.sampleParams(x, glm::vec3(10.0f, 1.5f, 0.0f));
    const glm::mat4 inv = glm::inverse(p.portals()[0].transform);
    const glm::vec3 other = glm::vec3(inv * glm::vec4(-9.81f, 0.0f, 0.0f, 0.0f));
    CHECK(near(mid.gravity, 0.5f * (glm::vec3(0.0f, -9.81f, 0.0f) + other), 1.0e-3f));
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
    CHECK(near(p.sampleParams(y, glm::vec3(110.0f, 1.0f, 0.0f)).gravity, glm::vec3(-9.81f, 0.0f, 0.0f), 1.0e-5f));
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
        CHECK(near(q.portals()[i].center(), p.portals()[i].center(), 1.0e-2f));
        CHECK(near(q.portals()[i].normal(), p.portals()[i].normal(), 1.0e-2f));
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

int main() {
    testTransformAndLoop();
    testObliqueClip();
    testFrameRecursion();
    testCrossingContinuity();
    testRaycastAndOverlap();
    testStraddlingObjects();
    testCellParamBlend();
    testJson();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
