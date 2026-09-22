#include <atomic>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <string>

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
}

#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

struct Health {
    int hp;
};

static bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1.0e-4f) { return glm::length(a - b) < eps; }

struct Counter : Script {
    int* starts;
    int* updates;
    int* destroys;
    Counter(int* s, int* u, int* d) : starts(s), updates(u), destroys(d) {}
    void onStart(World&, EntityHandle) override { ++*starts; }
    void onUpdate(World& w, EntityHandle e, float) override {
        ++*updates;
        w.destroy(e);
    }
    void onDestroy(World&, EntityHandle) override { ++*destroys; }
};

static void testHandles() {
    World w;
    auto a = w.create("a", 1);
    CHECK(w.isAlive(a));
    w.destroy(a);
    CHECK(!w.isAlive(a));
    auto b = w.create("b", 1);
    CHECK(b.index == a.index);
    CHECK(b.generation != a.generation);
    CHECK(!w.isAlive(a));
    CHECK(w.isAlive(b));
    w.setPosition(a, glm::vec3(9.0f));
    CHECK(w.getPosition(b).x == 0.0f);
    CHECK(w.aliveCount() == 1);
}

static void testHierarchy() {
    World w;
    auto p = w.create("p", 1);
    auto c1 = w.create("c1", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    auto c2 = w.create("c2", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    auto g = w.create("g", 1, glm::vec3(0.0f), glm::vec3(1.0f), c1);
    CHECK(w.getChildren(p).size() == 2);
    CHECK(w.getParent(g) == c1);
    CHECK(w.isDescendantOf(g, p));
    w.setParent(p, g);
    CHECK(w.getParent(p).isNull());
    w.setParent(g, c2);
    CHECK(w.getChildren(c1).empty());
    CHECK(w.getChildren(c2).size() == 1);
    CHECK(w.getRoots().size() == 1);
    w.destroy(p);
    CHECK(w.aliveCount() == 0);
    CHECK(!w.isAlive(g));
    CHECK(w.getRoots().empty());
}

static void testKeepWorldTransform() {
    World w;
    auto p = w.create("p", 1, glm::vec3(10.0f, 0.0f, 0.0f));
    auto c = w.create("c", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    w.setParent(c, p, true);
    const glm::vec3 wp = w.getWorldPosition(c);
    CHECK(wp.x == 1.0f && wp.y == 2.0f && wp.z == 3.0f);
    CHECK(w.getPosition(c).x == -9.0f);
    w.setParent(c, NULL_ENTITY, true);
    CHECK(w.getPosition(c).x == 1.0f);
}

static void testVisibility() {
    World w;
    auto e = w.create("e", 7);
    CHECK(w.isVisible(e));
    w.setVisible(e, false);
    CHECK(!w.isVisible(e));
    CHECK(w.getMesh(e) == 7);
    w.setMesh(e, 9);
    CHECK(w.getMesh(e) == 9);
    w.setVisible(e, true);
    CHECK(w.database().renderables[e.index].mesh_uuid == 9);
    CHECK((w.getRenderFlags(e) & RENDER_HIDDEN) == 0);
    w.setVisible(e, false);
    CHECK((w.getRenderFlags(e) & RENDER_HIDDEN) != 0);
    w.setRenderFlags(e, RENDER_NO_OCCLUDER, true);
    w.setRenderFlags(e, RENDER_HIDDEN, false);
    CHECK((w.getRenderFlags(e) & RENDER_HIDDEN) != 0);
    CHECK((w.getRenderFlags(e) & RENDER_NO_OCCLUDER) != 0);
    w.setVisible(e, true);
    CHECK(w.getRenderFlags(e) == RENDER_NO_OCCLUDER);
}

static void testNames() {
    World w;
    auto a = w.create("same", 1);
    auto b = w.create("same", 1);
    CHECK(w.findAll("same").size() == 2);
    w.destroy(a);
    CHECK(w.find("same") == b);
    CHECK(w.find("missing").isNull());
}

static void testComponents() {
    World w;
    auto a = w.create("a", 1);
    auto b = w.create("b", 1);
    auto c = w.create("c", 1);
    w.add<Health>(a, 10);
    w.add<Health>(b, 20);
    w.add<Health>(c, 30);
    int sum = 0;
    w.view<Health>([&](EntityHandle, Health& h) { sum += h.hp; });
    CHECK(sum == 60);
    w.destroy(a);
    CHECK(w.get<Health>(a) == nullptr);
    CHECK(w.get<Health>(b)->hp == 20);
    w.remove<Health>(c);
    CHECK(!w.has<Health>(c));
    auto d = w.create("d", 1);
    CHECK(!w.has<Health>(d));
}

static void testScripts() {
    World w;
    int starts = 0, updates = 0, destroys = 0;
    auto e = w.create("e", 1);
    w.attach<Counter>(e, &starts, &updates, &destroys);
    w.update(0.016f);
    CHECK(starts == 1);
    CHECK(updates == 1);
    CHECK(destroys == 1);
    CHECK(!w.isAlive(e));
    w.update(0.016f);
    CHECK(updates == 1);
}

static void testSaveLoad() {
    World w;
    auto a = w.create("root node", 3, glm::vec3(1.0f, 2.0f, 3.0f));
    auto b = w.create("leaf", 4, glm::vec3(4.0f, 5.0f, 6.0f), glm::vec3(2.0f), a);
    w.setVisible(b, false);
    w.setAlpha(b, 0.5f);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_test.litscene";
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(!w.isAlive(a));
    CHECK(w.loadScene(path));
    CHECK(w.aliveCount() == 2);
    auto ra = w.find("root node");
    auto rb = w.find("leaf");
    CHECK(!ra.isNull() && !rb.isNull());
    CHECK(w.getParent(rb) == ra);
    CHECK(!w.isVisible(rb));
    CHECK(w.getMesh(rb) == 4);
    CHECK(w.getRenderable(rb).alpha == 0.5f);
    CHECK(w.getPosition(ra).z == 3.0f);
    CHECK(!w.isAlive(a));
    CHECK(!w.isAlive(b));
    std::filesystem::remove(path);
}

static void testComponentSerialization() {
    World w;
    w.registerComponent<Health>(
        "Health", [](const Health& h, std::ostream& out) { out << h.hp; },
        [](std::istream& in) -> std::optional<Health> {
            int hp;
            if (!(in >> hp)) return std::nullopt;
            return Health{hp};
        });
    auto a = w.create("a", 1);
    auto b = w.create("b", 1);
    w.add<Health>(b, 42);
    (void)a;
    const auto path = std::filesystem::temp_directory_path() / "lit_world_comp_test.litscene";
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    auto rb = w.find("b");
    CHECK(w.get<Health>(rb) && w.get<Health>(rb)->hp == 42);
    CHECK(!w.has<Health>(w.find("a")));
    std::filesystem::remove(path);
}

static void testMeshRemap() {
    World w;
    w.setMeshHooks(
        [](uint32_t id) { return id == 5 ? std::string("cube") : std::string(); },
        [](const std::string& name) { return name == "cube" ? 77u : 0u; });
    w.create("a", 5);
    w.create("b", 9);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_mesh_test.litscene";
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    CHECK(w.getMesh(w.find("a")) == 77);
    CHECK(w.getMesh(w.find("b")) == 9);
    std::filesystem::remove(path);
}

static void testPrefab() {
    World w;
    w.registerComponent<Health>(
        "Health", [](const Health& h, std::ostream& out) { out << h.hp; },
        [](std::istream& in) -> std::optional<Health> {
            int hp;
            if (!(in >> hp)) return std::nullopt;
            return Health{hp};
        });
    auto root = w.create("root", 1, glm::vec3(1.0f, 0.0f, 0.0f));
    auto child = w.create("child", 2, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), root);
    w.add<Health>(child, 5);
    w.setVisible(child, false);
    const Prefab prefab = w.capture(root);
    CHECK(prefab.nodes.size() == 2);

    auto holder = w.create("holder", 0, glm::vec3(100.0f, 0.0f, 0.0f));
    auto copy = w.instantiate(prefab, holder);
    CHECK(w.isAlive(copy));
    CHECK(w.getParent(copy) == holder);
    CHECK(w.aliveCount() == 5);
    auto copyChildren = w.getChildren(copy);
    CHECK(copyChildren.size() == 1);
    CHECK(w.getName(copyChildren[0]) == "child");
    CHECK(!w.isVisible(copyChildren[0]));
    CHECK(w.get<Health>(copyChildren[0]) && w.get<Health>(copyChildren[0])->hp == 5);
    CHECK(w.getWorldPosition(copy).x == 101.0f);
    CHECK(w.capture(NULL_ENTITY).empty());
}

static void testLayersAndTags() {
    World w;
    auto a = w.create("a", 1);
    auto b = w.create("b", 1);
    auto c = w.create("c", 1);
    w.setLayer(b, 0b10);
    w.setLayer(c, 0b110);
    int hits = 0;
    w.forEachInLayer(0b10, [&](EntityHandle) { ++hits; });
    CHECK(hits == 2);
    w.setRenderFlags(c, RENDER_NO_OCCLUDER, true);
    w.setTag(a, "enemy");
    w.setTag(b, "enemy");
    CHECK(w.findByTag("enemy").size() == 2);
    w.setTag(a, "ally");
    CHECK(w.findByTag("enemy").size() == 1);
    w.destroy(b);
    CHECK(w.findByTag("enemy").empty());
    CHECK(w.getTag(a) == "ally");
    const Prefab prefab = w.capture(a);
    auto copy = w.instantiate(prefab);
    CHECK(w.getTag(copy) == "ally");
    CHECK(w.findByTag("ally").size() == 2);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_tag_test.litscene";
    w.setLayer(c, 0b1000);
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    CHECK(w.findByTag("ally").size() == 2);
    CHECK(w.getLayer(w.find("c")) == 0b1000);
    CHECK(w.getRenderFlags(w.find("c")) == RENDER_NO_OCCLUDER);
    std::filesystem::remove(path);
}

static void testWorldCache() {
    World w;
    auto a = w.create("a", 1, glm::vec3(1.0f, 0.0f, 0.0f));
    auto b = w.create("b", 1, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), a);
    auto c = w.create("c", 1, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f), b);
    CHECK(w.getWorldPosition(c).x == 1.0f && w.getWorldPosition(c).y == 1.0f && w.getWorldPosition(c).z == 1.0f);
    w.setPosition(a, glm::vec3(10.0f, 0.0f, 0.0f));
    CHECK(w.getWorldPosition(c).x == 10.0f);
    w.setPosition(b, glm::vec3(0.0f, 5.0f, 0.0f));
    CHECK(w.getWorldPosition(c).y == 5.0f);
    auto d = w.create("d", 1, glm::vec3(100.0f, 0.0f, 0.0f));
    w.setParent(c, d, false);
    CHECK(w.getWorldPosition(c).x == 100.0f);
    w.setPosition(d, glm::vec3(200.0f, 0.0f, 0.0f));
    CHECK(w.getWorldPosition(c).x == 200.0f);
    w.destroy(d);
    auto e = w.create("e", 1, glm::vec3(7.0f, 0.0f, 0.0f));
    CHECK(w.getWorldPosition(e).x == 7.0f);
}

static void testEvents() {
    World w;
    int created = 0, destroyed = 0, reparented = 0;
    const uint32_t id = w.events().subscribe<EntityCreated>([&](const EntityCreated&) { ++created; });
    w.events().subscribe<EntityDestroyed>([&](const EntityDestroyed&) { ++destroyed; });
    w.events().subscribe<EntityReparented>([&](const EntityReparented& e) { if (!e.newParent.isNull()) ++reparented; });
    auto a = w.create("a", 1);
    auto b = w.create("b", 1);
    CHECK(created == 0);
    w.events().dispatch();
    CHECK(created == 2);
    w.setParent(b, a);
    w.destroy(a);
    w.events().dispatch();
    CHECK(reparented == 1);
    CHECK(destroyed == 2);
    w.events().unsubscribe(id);
    w.create("c", 1);
    w.events().dispatch();
    CHECK(created == 2);
    CHECK(w.events().pending() == 0);
}

struct FixedCounter : Script {
    int* fixed;
    int* starts;
    FixedCounter(int* f, int* s) : fixed(f), starts(s) {}
    void onStart(World&, EntityHandle) override { ++*starts; }
    void onFixedUpdate(World&, EntityHandle, float) override { ++*fixed; }
};

static void testFixedUpdate() {
    World w;
    int fixed = 0, starts = 0;
    auto e = w.create("e", 1);
    w.attach<FixedCounter>(e, &fixed, &starts);
    w.fixedUpdate(0.016f);
    w.fixedUpdate(0.016f);
    CHECK(fixed == 2);
    CHECK(starts == 1);
}

static void testCameraEntity() {
    World w;
    auto cam = w.create("cam", 0, glm::vec3(3.0f, 4.0f, 5.0f));
    w.add<CameraComponent>(cam, 60.0f, 0.5f, 250.0f);
    w.setActiveCamera(cam);
    w.syncCamera();
    CHECK(w.camera().getPosition().x == 3.0f && w.camera().getPosition().z == 5.0f);
    CHECK(std::abs(w.camera().getYaw() + 90.0f) < 0.01f);
    CHECK(std::abs(w.camera().getPitch()) < 0.01f);
    CHECK(w.camera().getFov() == 60.0f);
    CHECK(w.camera().getFarPlane() == 250.0f);
    w.setRotation(cam, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    w.syncCamera();
    CHECK(std::abs(w.camera().getYaw() - 180.0f) < 0.01f || std::abs(w.camera().getYaw() + 180.0f) < 0.01f);
    w.destroy(cam);
    CHECK(w.getActiveCamera().isNull());
}

static void testSpatial() {
    World w;
    w.setMeshBoundsHook([](uint32_t mesh) { return mesh == 99 ? glm::vec4(0.0f, 0.0f, 0.0f, 1000.0f) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    auto near = w.create("near", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    auto far = w.create("far", 1, glm::vec3(0.0f, 0.0f, -50.0f));
    auto side = w.create("side", 1, glm::vec3(100.0f, 0.0f, 0.0f));

    auto hit = w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == near);
    CHECK(hit && std::abs(hit->distance - 9.0f) < 0.001f);
    CHECK(!w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(!w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 5.0f));

    CHECK(w.overlapSphere(glm::vec3(0.0f), 12.0f).size() == 1);
    CHECK(w.overlapSphere(glm::vec3(0.0f), 60.0f).size() == 2);
    CHECK(w.overlapSphere(glm::vec3(100.0f, 0.0f, 0.0f), 0.5f).size() == 1);

    w.setPosition(near, glm::vec3(0.0f, 30.0f, -10.0f));
    hit = w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == far);

    w.setVisible(far, false);
    CHECK(!w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
    w.setVisible(far, true);
    w.destroy(far);
    CHECK(!w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));

    auto parent = w.create("parent", 0, glm::vec3(0.0f, 0.0f, -200.0f));
    auto child = w.create("child", 1, glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(1.0f), parent);
    w.setVisible(parent, false);
    hit = w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == child);
    w.setPosition(parent, glm::vec3(500.0f, 0.0f, -200.0f));
    CHECK(!w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
    hit = w.raycast(glm::vec3(500.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == child);

    auto ground = w.create("ground", 99);
    CHECK(w.overlapSphere(glm::vec3(0.0f, -900.0f, 0.0f), 1.0f).size() == 1);
    (void)side;
}

static void testScreenRay() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    w.camera().setPos(glm::vec3(0.0f, 0.0f, 10.0f));
    w.camera().setOrientation(-90.0f, 0.0f);
    w.camera().updateAspectRatio(1280.0f, 720.0f);
    const Ray center = w.screenRay(640.0f, 360.0f, 1280.0f, 720.0f);
    CHECK(std::abs(center.direction.z + 1.0f) < 0.001f);
    CHECK(std::abs(center.direction.x) < 0.001f && std::abs(center.direction.y) < 0.001f);
    const Ray right = w.screenRay(1000.0f, 360.0f, 1280.0f, 720.0f);
    CHECK(right.direction.x > 0.05f);
    const Ray top = w.screenRay(640.0f, 100.0f, 1280.0f, 720.0f);
    CHECK(top.direction.y > 0.05f);

    auto target = w.create("target", 1, glm::vec3(0.0f, 0.0f, 0.0f));
    auto hit = w.raycast(center.origin, center.direction);
    CHECK(hit && hit->entity == target);
    auto offside = w.create("offside", 1, glm::vec3(3.0f, 0.0f, 0.0f));
    const Ray toOffside = w.screenRay(640.0f + 640.0f * (3.0f / 10.0f) / std::tan(glm::radians(22.5f)) / (1280.0f / 720.0f), 360.0f, 1280.0f, 720.0f);
    hit = w.raycast(toOffside.origin, toOffside.direction);
    CHECK(hit && hit->entity == offside);
}

static void testTrimAndCompact() {
    World w;
    std::vector<EntityHandle> handles;
    for (int i = 0; i < 100; ++i) handles.push_back(w.create("e" + std::to_string(i), 1, glm::vec3(float(i), 0.0f, 0.0f)));
    for (int i = 50; i < 100; ++i) w.destroy(handles[i]);
    CHECK(w.database().transforms.size() == 50);
    CHECK(w.aliveCount() == 50);
    CHECK(!w.isAlive(handles[99]));
    auto fresh = w.create("fresh", 1);
    CHECK(fresh.index == 50);
    CHECK(fresh.generation > handles[50].generation);
    CHECK(!w.isAlive(handles[50]));
    w.destroy(fresh);

    for (int i = 0; i < 50; i += 2) w.destroy(handles[i]);
    CHECK(w.aliveCount() == 25);
    CHECK(w.database().transforms.size() > 25);

    auto parent = w.find("e49");
    auto childOfLast = w.create("child", 1, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), parent);
    w.add<Health>(childOfLast, 7);
    w.setTag(childOfLast, "moved");
    w.setVisible(childOfLast, false);

    const auto moved = w.compact();
    CHECK(!moved.empty());
    CHECK(w.database().transforms.size() == w.aliveCount());
    CHECK(w.database().hierarchies.size() == w.aliveCount());
    CHECK(!w.isAlive(childOfLast));

    auto rc = w.find("child");
    auto rp = w.find("e49");
    CHECK(!rc.isNull() && !rp.isNull());
    CHECK(w.getParent(rc) == rp);
    CHECK(w.getChildren(rp).size() == 1);
    CHECK(w.get<Health>(rc) && w.get<Health>(rc)->hp == 7);
    CHECK(w.findByTag("moved").size() == 1 && w.findByTag("moved")[0] == rc);
    CHECK(!w.isVisible(rc));
    CHECK(w.getWorldPosition(rc).x == 49.0f && w.getWorldPosition(rc).y == 5.0f);
    CHECK(w.database().renderables[rc.index].objectId == rc.index);
    for (const EntityMoved& m : moved) {
        CHECK(!w.isAlive(m.from));
        CHECK(w.isAlive(m.to));
    }
    size_t roots = w.getRoots().size();
    CHECK(roots == w.aliveCount() - 1);
}

static void testCompactKeepsSpatialAndScripts() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    auto a = w.create("a", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    auto b = w.create("b", 1, glm::vec3(0.0f, 0.0f, -30.0f));
    auto c = w.create("c", 1, glm::vec3(0.0f, 0.0f, -50.0f));
    CHECK(w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f))->entity == a);
    int starts = 0, updates = 0, destroys = 0;
    w.attach<Counter>(c, &starts, &updates, &destroys);
    w.setActiveCamera(c);
    w.destroy(a);
    w.destroy(b);
    w.compact();
    auto rc = w.find("c");
    CHECK(rc.index == 0);
    auto hit = w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == rc);
    CHECK(w.getActiveCamera() == rc);
    w.update(0.016f);
    CHECK(starts == 1 && updates == 1 && destroys == 1);
    CHECK(w.aliveCount() == 0);
}

static void testBatch() {
    World w;
    std::vector<EntityHandle> handles;
    for (int i = 0; i < 6000; ++i) handles.push_back(w.create("e", 1, glm::vec3(float(i), 0.0f, 0.0f)));

    std::vector<std::pair<EntityHandle, glm::vec3>> moves;
    for (int i = 0; i < 6000; ++i) moves.emplace_back(handles[i], glm::vec3(0.0f, float(i), 1.0f));
    const auto start = std::chrono::steady_clock::now();
    w.setPositions(moves);
    const auto mid = std::chrono::steady_clock::now();
    CHECK(w.getPosition(handles[5999]).y == 5999.0f);
    CHECK(w.getPosition(handles[0]).x == 0.0f && w.getPosition(handles[0]).z == 1.0f);

    std::vector<std::pair<EntityHandle, glm::vec3>> few{{handles[10], glm::vec3(7.0f)}, {NULL_ENTITY, glm::vec3(1.0f)}};
    w.setPositions(few);
    CHECK(w.getPosition(handles[10]).x == 7.0f);

    std::vector<EntityHandle> doomed(handles.begin() + 3000, handles.end());
    doomed.push_back(handles[3000]);
    w.destroyBatch(doomed);
    const auto end = std::chrono::steady_clock::now();
    CHECK(w.aliveCount() == 3000);
    CHECK(w.database().transforms.size() == 3000);
    CHECK(!w.isAlive(handles[4000]));
    CHECK(w.isAlive(handles[2999]));
    std::printf("batch setPositions %.3f ms, destroyBatch %.3f ms\n", std::chrono::duration<double, std::milli>(mid - start).count(), std::chrono::duration<double, std::milli>(end - mid).count());
}

static void testAnimationAgreement() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 0.5f); });
    auto root = w.create("root", 0);
    std::vector<EntityHandle> movers;
    std::vector<glm::vec3> base;
    for (int i = 0; i < 4; ++i) {
        base.push_back(glm::vec3(float(i) * 10.0f, 0.0f, -20.0f));
        movers.push_back(w.create("m", 1, base.back()));
    }
    auto follower = w.create("follower", 1, glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(1.0f), movers[2]);
    w.setAnimation(movers[0].index, base);
    w.setAnimationTime(1.25f);
    for (uint32_t i = 0; i < 4; ++i) {
        const glm::vec3 expected = base[i] + World::orbitOffset(1.25f, i);
        const glm::vec3 actual = w.getWorldPosition(movers[i]);
        CHECK(glm::length(actual - expected) < 1.0e-4f);
    }
    CHECK(glm::length(w.getWorldPosition(follower) - (base[2] + World::orbitOffset(1.25f, 2) + glm::vec3(0.0f, 3.0f, 0.0f))) < 1.0e-4f);

    const glm::vec3 target = base[1] + World::orbitOffset(1.25f, 1);
    auto hit = w.raycast(target + glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == movers[1]);

    w.setAnimationTime(4.0f);
    const glm::vec3 moved = base[1] + World::orbitOffset(4.0f, 1);
    CHECK(glm::length(w.getWorldPosition(movers[1]) - moved) < 1.0e-4f);
    hit = w.raycast(moved + glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == movers[1]);
    (void)root;
}

struct Patrol : Script {
    int waypoint;
    float speed;
    Patrol(int w, float s) : waypoint(w), speed(s) {}
};

static void registerPatrol(World& w) {
    w.registerScript<Patrol>(
        "Patrol", [](const Patrol& p, std::ostream& out) { out << p.waypoint << ' ' << p.speed; },
        [](std::istream& in) -> std::optional<Patrol> {
            int waypoint;
            float speed;
            if (!(in >> waypoint >> speed)) return std::nullopt;
            return Patrol(waypoint, speed);
        });
}

static void testScriptSerialization() {
    World w;
    registerPatrol(w);
    auto a = w.create("guard", 1);
    auto b = w.create("plain", 1);
    w.attach<Patrol>(a, 3, 2.5f);
    int s = 0, u = 0, d = 0;
    w.attach<Counter>(b, &s, &u, &d);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_script_test.litscene";
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    auto ra = w.find("guard");
    auto* patrol = w.getScript<Patrol>(ra);
    CHECK(patrol && patrol->waypoint == 3 && patrol->speed == 2.5f);
    CHECK(w.getScript<Counter>(w.find("plain")) == nullptr);
    std::filesystem::remove(path);

    const Prefab prefab = w.capture(ra);
    CHECK(prefab.nodes.size() == 1 && prefab.nodes[0].scripts.size() == 1);
    auto copy = w.instantiate(prefab);
    auto* copied = w.getScript<Patrol>(copy);
    CHECK(copied && copied != patrol && copied->waypoint == 3);
}

struct TickProbe : Script {
    int* updates;
    float* lastDelta;
    int* fixedCount;
    TickProbe(int* u, float* d, int* f) : updates(u), lastDelta(d), fixedCount(f) {}
    void onUpdate(World&, EntityHandle, float dt) override {
        ++*updates;
        *lastDelta = dt;
    }
    void onFixedUpdate(World&, EntityHandle, float) override { ++*fixedCount; }
};

static void testTimeService() {
    Engine engine;
    World& w = engine.world();
    int updates = 0, fixedCount = 0;
    float lastDelta = 0.0f;
    auto e = w.create("e", 1);
    w.attach<TickProbe>(e, &updates, &lastDelta, &fixedCount);

    engine.tick(0.25f);
    CHECK(updates == 1 && std::abs(lastDelta - 0.25f) < 1.0e-6f);
    CHECK(fixedCount >= 14 && fixedCount <= 15);
    CHECK(engine.time().frame == 1);
    CHECK(std::abs(engine.time().elapsed - 0.25) < 1.0e-6);

    engine.setTimeScale(0.5f);
    engine.tick(0.1f);
    CHECK(std::abs(lastDelta - 0.05f) < 1.0e-6f);
    CHECK(std::abs(engine.time().unscaledDeltaTime - 0.1f) < 1.0e-6f);
    CHECK(std::abs(engine.time().elapsed - 0.30) < 1.0e-6);
    CHECK(std::abs(engine.time().unscaledElapsed - 0.35) < 1.0e-6);

    engine.setPaused(true);
    const int updatesBefore = updates;
    const int fixedBefore = fixedCount;
    engine.tick(0.1f);
    CHECK(updates == updatesBefore && fixedCount == fixedBefore);
    CHECK(engine.time().deltaTime == 0.0f);
    CHECK(engine.time().frame == 3);
    CHECK(std::abs(engine.time().elapsed - 0.30) < 1.0e-6);

    engine.setPaused(false);
    engine.tick(1.0f);
    CHECK(std::abs(engine.time().unscaledDeltaTime - 0.25f) < 1.0e-6f);
    CHECK(std::abs(engine.time().deltaTime - 0.125f) < 1.0e-6f);
}

static void testLights() {
    World w;
    LightSet lights;
    CHECK(!collectLights(w, glm::vec3(0.0f), lights));

    auto sun = w.create("sun");
    w.add<LightComponent>(sun, LightComponent{LightComponent::Type::Directional, glm::vec3(1.0f, 0.5f, 0.25f), 2.0f, 0.0f, 0.5f});
    w.setRotation(sun, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    auto far = w.create("far", NO_MESH, glm::vec3(100.0f, 0.0f, 0.0f));
    auto mid = w.create("mid", NO_MESH, glm::vec3(10.0f, 0.0f, 0.0f));
    auto closest = w.create("near", NO_MESH, glm::vec3(1.0f, 0.0f, 0.0f));
    w.add<LightComponent>(far, LightComponent{LightComponent::Type::Point, glm::vec3(1.0f), 1.0f, 50.0f, 1.0f});
    w.add<LightComponent>(mid, LightComponent{LightComponent::Type::Point, glm::vec3(0.0f, 1.0f, 0.0f), 3.0f, 40.0f, 1.0f});
    w.add<LightComponent>(closest, LightComponent{LightComponent::Type::Point, glm::vec3(0.0f, 0.0f, 1.0f), 4.0f, 30.0f, 1.0f});

    CHECK(collectLights(w, glm::vec3(0.0f), lights));
    CHECK(std::abs(lights.directional[0].y - 1.0f) < 1.0e-4f && std::abs(lights.directional[0].x) < 1.0e-4f);
    CHECK(lights.directional[0].w == 0.5f);
    CHECK(lights.directional[1].x == 2.0f && lights.directional[1].y == 1.0f && lights.directional[1].z == 0.5f);
    CHECK(lights.count == 3 && lights.packed.size() == 12);
    CHECK(lights.packed[0].x == 1.0f && lights.packed[0].w == 30.0f && lights.packed[1].w == 4.0f && lights.packed[1].z == 1.0f);
    CHECK(lights.packed[3].y == 0.0f);
    CHECK(lights.packed[4].x == 10.0f && lights.packed[5].y == 1.0f);
    CHECK(lights.packed[8].x == 100.0f);

    w.destroy(closest);
    w.destroy(mid);
    CHECK(collectLights(w, glm::vec3(0.0f), lights));
    CHECK(lights.count == 1 && lights.packed[0].x == 100.0f);

    World spots;
    auto lamp = spots.create("lamp", NO_MESH, glm::vec3(0.0f, 5.0f, 0.0f));
    spots.setRotation(lamp, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    spots.add<LightComponent>(lamp, LightComponent{LightComponent::Type::Spot, glm::vec3(1.0f), 2.0f, 20.0f, 1.0f, 10.0f, 40.0f});
    CHECK(collectLights(spots, glm::vec3(0.0f), lights));
    CHECK(lights.count == 1 && lights.packed[3].y == 1.0f);
    CHECK(near(glm::vec3(lights.packed[2]), glm::vec3(0.0f, -1.0f, 0.0f)));
    CHECK(std::abs(lights.packed[2].w - std::cos(glm::radians(40.0f))) < 1.0e-5f);
    CHECK(std::abs(lights.packed[3].x - std::cos(glm::radians(10.0f))) < 1.0e-5f);

    World many;
    for (int i = 0; i < 100; ++i) {
        auto l = many.create("l", NO_MESH, glm::vec3(float(i), 0.0f, 0.0f));
        many.add<LightComponent>(l, LightComponent{});
    }
    CHECK(collectLights(many, glm::vec3(0.0f), lights, 64));
    CHECK(lights.count == 64 && lights.packed.size() == 256);
    CHECK(lights.packed[63 * 4].x == 63.0f);
    CHECK(collectLights(many, glm::vec3(99.0f, 0.0f, 0.0f), lights, 8));
    CHECK(lights.count == 8 && lights.packed[0].x == 99.0f);
}

static void testFrustumQuery() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    w.camera().setPos(glm::vec3(0.0f));
    w.camera().setOrientation(-90.0f, 0.0f);
    w.camera().updateAspectRatio(16.0f, 9.0f);
    w.camera().setNearPlane(0.1f);
    w.camera().setFarPlane(100.0f);

    auto front = w.create("front", 1, glm::vec3(0.0f, 0.0f, -10.0f));
    auto behind = w.create("behind", 1, glm::vec3(0.0f, 0.0f, 10.0f));
    auto beyond = w.create("beyond", 1, glm::vec3(0.0f, 0.0f, -200.0f));
    auto wide = w.create("wide", 1, glm::vec3(60.0f, 0.0f, -10.0f));
    auto edge = w.create("edge", 1, glm::vec3(7.6f, 0.0f, -10.0f));
    auto above = w.create("above", 1, glm::vec3(0.0f, 30.0f, -10.0f));

    const auto visible = w.queryFrustum(w.camera());
    const auto has = [&](EntityHandle e) { return std::find(visible.begin(), visible.end(), e) != visible.end(); };
    CHECK(has(front));
    CHECK(has(edge));
    CHECK(!has(behind));
    CHECK(!has(beyond));
    CHECK(!has(wide));
    CHECK(!has(above));
    CHECK(visible.size() == 2);

    w.camera().setOrientation(90.0f, 0.0f);
    const auto turned = w.queryFrustum(w.camera());
    CHECK(turned.size() == 1 && turned[0] == behind);
}

static void testPhysics() {
    World w;
    PhysicsSettings settings;
    auto ground = w.create("ground", 0);
    w.add<PlaneCollider>(ground);
    auto ball = w.create("ball", 1, glm::vec3(0.0f, 5.0f, 0.0f));
    w.add<SphereCollider>(ball, 0.5f);
    w.add<RigidBody>(ball);
    int collisions = 0;
    w.events().subscribe<Collision>([&](const Collision&) { ++collisions; });

    for (int i = 0; i < 240; ++i) stepPhysics(w, 1.0f / 60.0f, settings);
    w.events().dispatch();
    CHECK(std::abs(w.getPosition(ball).y - 0.5f) < 0.01f);
    CHECK(std::abs(w.get<RigidBody>(ball)->velocity.y) < 0.5f);
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

static void testAdditiveLoad() {
    World w;
    auto root = w.create("root", 1, glm::vec3(1.0f, 0.0f, 0.0f));
    w.create("leaf", 2, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), root);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_additive_test.litscene";
    CHECK(w.saveScene(path));

    auto anchor = w.create("anchor", 0, glm::vec3(100.0f, 0.0f, 0.0f));
    const auto first = w.loadSceneAdditive(path, anchor);
    CHECK(first && first->size() == 1);
    CHECK(w.aliveCount() == 5);
    CHECK(first && w.getParent((*first)[0]) == anchor);

    const auto second = w.loadSceneAdditive(path);
    CHECK(second && second->size() == 1);
    CHECK(w.aliveCount() == 7);
    CHECK(second && w.getParent((*second)[0]).isNull());
    CHECK(w.findAll("leaf").size() == 3);

    w.unloadGroup(*first);
    CHECK(w.aliveCount() == 5);
    CHECK(w.isAlive(anchor));
    CHECK(w.findAll("leaf").size() == 2);
    CHECK(!w.loadSceneAdditive(path.string() + ".missing"));
    CHECK(w.aliveCount() == 5);
    std::filesystem::remove(path);
}

struct Link {
    EntityHandle target;
};

static void registerLink(World& w) {
    w.registerComponent<Link>(
        "Link", [](const Link& l, std::ostream& out) { out << l.target.index; },
        [](std::istream& in, const LoadContext& ctx) -> std::optional<Link> {
            uint32_t index;
            if (!(in >> index)) return std::nullopt;
            return Link{ctx.resolve(index)};
        });
}

static void testEntityReferences() {
    World w;
    registerLink(w);
    auto pad = w.create("pad", 0);
    auto a = w.create("a", 1);
    auto b = w.create("b", 1, glm::vec3(0.0f), glm::vec3(1.0f), a);
    w.add<Link>(a, Link{b});
    w.add<Link>(b, Link{a});
    w.destroy(pad);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_link_test.litscene";
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    auto ra = w.find("a");
    auto rb = w.find("b");
    CHECK(w.get<Link>(ra) && w.get<Link>(ra)->target == rb);
    CHECK(w.get<Link>(rb) && w.get<Link>(rb)->target == ra);
    std::filesystem::remove(path);

    const Prefab prefab = w.capture(ra);
    auto copy = w.instantiate(prefab);
    auto copyChildren = w.getChildren(copy);
    CHECK(copyChildren.size() == 1);
    CHECK(w.get<Link>(copy) && w.get<Link>(copy)->target == copyChildren[0]);
    CHECK(w.get<Link>(copyChildren[0]) && w.get<Link>(copyChildren[0])->target == copy);
    CHECK(w.get<Link>(ra)->target == rb);
}

static void testHistory() {
    World w;
    History history(w);

    auto a = history.create(EntityDesc{.name = "a", .mesh = 1, .position = glm::vec3(1.0f, 0.0f, 0.0f)});
    auto b = history.create(EntityDesc{.name = "b", .mesh = 1, .position = glm::vec3(0.0f, 2.0f, 0.0f)});
    CHECK(w.aliveCount() == 2);

    history.setLocalMatrix(a, glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 0.0f, 0.0f)));
    CHECK(w.getPosition(a).x == 9.0f);
    CHECK(history.undo());
    CHECK(w.getPosition(a).x == 1.0f);
    CHECK(history.redo());
    CHECK(w.getPosition(a).x == 9.0f);
    CHECK(!history.redo());

    history.setVisible(b, false);
    CHECK(!w.isVisible(b));
    history.undo();
    CHECK(w.isVisible(b));

    history.setParent(b, a, false);
    CHECK(w.getParent(b) == a);
    history.undo();
    CHECK(w.getParent(b).isNull());
    history.redo();
    CHECK(w.getParent(b) == a);

    history.destroy(a);
    CHECK(w.aliveCount() == 0);
    CHECK(history.undo());
    CHECK(w.aliveCount() == 2);
    auto ra = w.find("a");
    auto rb = w.find("b");
    CHECK(!ra.isNull() && !rb.isNull());
    CHECK(w.getParent(rb) == ra);
    CHECK(w.getPosition(ra).x == 9.0f);
    CHECK(history.resolve(a) == ra);
    CHECK(history.resolve(b) == rb);

    history.setLocalMatrix(b, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f)));
    CHECK(w.getPosition(rb).z == 5.0f);
    history.undo();
    CHECK(w.getPosition(rb).z == 0.0f && w.getPosition(rb).y == 2.0f);
    history.undo();
    CHECK(w.getParent(w.find("b")).isNull());
    history.undo();
    CHECK(w.getPosition(w.find("a")).x == 1.0f);
    history.undo();
    CHECK(w.aliveCount() == 1);
    history.undo();
    CHECK(w.aliveCount() == 0);
    CHECK(!history.undo());
    history.redo();
    history.redo();
    CHECK(w.aliveCount() == 2);

    history.create(EntityDesc{.name = "c", .mesh = 1});
    CHECK(!history.canRedo());
}

static void testSiblingIteration() {
    World w;
    auto p = w.create("p", 1);
    auto r2 = w.create("r2", 1);
    auto c1 = w.create("c1", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    auto c2 = w.create("c2", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    size_t roots = 0;
    for (EntityHandle r = w.firstRoot(); !r.isNull(); r = w.nextSibling(r)) ++roots;
    CHECK(roots == 2);
    size_t kids = 0;
    for (EntityHandle c = w.firstChild(p); !c.isNull(); c = w.nextSibling(c)) ++kids;
    CHECK(kids == 2);
    CHECK(w.firstChild(r2).isNull());
    CHECK(w.firstChild(NULL_ENTITY).isNull());
    (void)c1;
    (void)c2;
}

static void testSnapshotRestore() {
    World w;
    registerLink(w);
    auto a = w.create("a", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    auto b = w.create("b", 1, glm::vec3(0.0f), glm::vec3(1.0f), a);
    w.add<Link>(b, Link{a});
    w.setTag(b, "tagged");
    const std::string saved = w.snapshot();
    CHECK(!saved.empty());

    w.setPosition(a, glm::vec3(50.0f));
    w.destroy(b);
    w.create("extra", 1);
    CHECK(w.aliveCount() == 2);

    CHECK(w.restore(saved));
    CHECK(w.aliveCount() == 2);
    auto ra = w.find("a");
    auto rb = w.find("b");
    CHECK(w.getPosition(ra).x == 1.0f && w.getPosition(ra).z == 3.0f);
    CHECK(w.getParent(rb) == ra);
    CHECK(w.get<Link>(rb) && w.get<Link>(rb)->target == ra);
    CHECK(w.find("extra").isNull());
    CHECK(!w.isAlive(a));
    CHECK(!w.restore("not a scene"));
}

static void testMeshlessEntities() {
    World w;
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    auto empty = w.create("empty");
    CHECK(w.getMesh(empty) == NO_MESH);
    EntityDesc desc;
    CHECK(desc.mesh == NO_MESH);
    w.setPosition(empty, glm::vec3(0.0f, 0.0f, -5.0f));
    auto solid = w.create("solid", 1, glm::vec3(0.0f, 0.0f, -20.0f));
    auto hit = w.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == solid);
    CHECK(w.overlapSphere(glm::vec3(0.0f, 0.0f, -5.0f), 2.0f).empty());
    CHECK(w.getWorldBounds(empty).w == 0.0f);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_meshless_test.litscene";
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    CHECK(w.getMesh(w.find("empty")) == NO_MESH);
    CHECK(w.getMesh(w.find("solid")) == 1);
    std::filesystem::remove(path);

    w.setMesh(w.find("empty"), 3);
    CHECK(w.getMesh(w.find("empty")) == 3);
}

struct Tint {
    float r;
};

static void testEachAndBuilder() {
    World w;
    auto a = w.spawn().name("a").mesh(1).at(glm::vec3(1.0f, 2.0f, 3.0f)).with<Health>(10).with<Tint>(0.5f).build();
    auto b = w.spawn().name("b").with<Health>(20).tag("t").layer(0b100).visible(false).build();
    auto c = w.spawn().name("c").with<Tint>(0.25f).build();
    auto d = w.spawn().name("d").mesh(2).with<Health>(30).with<Tint>(0.75f).with<Link>(a).build();
    CHECK(w.getName(a) == "a" && w.getMesh(a) == 1 && w.getPosition(a).z == 3.0f);
    CHECK(w.getTag(b) == "t" && w.getLayer(b) == 0b100 && !w.isVisible(b));

    int both = 0;
    float sum = 0.0f;
    w.each<Health, Tint>([&](EntityHandle e, Health& h, Tint& t) {
        ++both;
        sum += float(h.hp) * t.r;
        CHECK(e == a || e == d);
    });
    CHECK(both == 2);
    CHECK(std::abs(sum - (10 * 0.5f + 30 * 0.75f)) < 1.0e-5f);

    int triple = 0;
    w.each<Link, Health, Tint>([&](EntityHandle e, Link& l, Health& h, Tint& t) {
        ++triple;
        CHECK(e == d && l.target == a && h.hp == 30 && t.r == 0.75f);
    });
    CHECK(triple == 1);

    int single = 0;
    w.each<Tint>([&](EntityHandle, Tint& t) {
        ++single;
        t.r += 1.0f;
    });
    CHECK(single == 3);
    CHECK(w.get<Tint>(c)->r == 1.25f);

    int none = 0;
    w.each<Link, Tint, Health>([&](EntityHandle, Link&, Tint&, Health&) { ++none; });
    CHECK(none == 1);
    w.remove<Link>(d);
    none = 0;
    w.each<Link, Tint, Health>([&](EntityHandle, Link&, Tint&, Health&) { ++none; });
    CHECK(none == 0);

    EntityHandle implicit = w.spawn().name("implicit");
    CHECK(w.isAlive(implicit) && w.getName(implicit) == "implicit");
    (void)b;
}

static void testTransformConveniences() {
    World w;
    auto e = w.create("e", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(near(w.forward(e), glm::vec3(0.0f, 0.0f, -1.0f)));
    CHECK(near(w.right(e), glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(near(w.up(e), glm::vec3(0.0f, 1.0f, 0.0f)));

    w.rotate(e, glm::vec3(0.0f, 1.0f, 0.0f), glm::radians(90.0f));
    CHECK(near(w.forward(e), glm::vec3(-1.0f, 0.0f, 0.0f)));
    CHECK(near(w.right(e), glm::vec3(0.0f, 0.0f, -1.0f)));

    w.lookAt(e, glm::vec3(1.0f, 2.0f, -10.0f));
    CHECK(near(w.forward(e), glm::vec3(0.0f, 0.0f, -1.0f)));
    CHECK(near(w.getWorldPosition(e), glm::vec3(1.0f, 2.0f, 3.0f)));
    w.lookAt(e, glm::vec3(11.0f, 2.0f, 3.0f));
    CHECK(near(w.forward(e), glm::vec3(1.0f, 0.0f, 0.0f)));
    w.lookAt(e, glm::vec3(1.0f, 12.0f, 3.0f));
    CHECK(near(w.forward(e), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(glm::length(w.up(e)) > 0.99f);

    auto parent = w.create("parent", 0, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(2.0f));
    w.setRotation(parent, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
    auto child = w.create("child", 1, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), parent);
    CHECK(near(w.getWorldPosition(child), glm::vec3(10.0f, 2.0f, 0.0f)));
    CHECK(near(w.right(child), glm::vec3(0.0f, 1.0f, 0.0f)));

    w.setWorldPosition(child, glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK(near(w.getWorldPosition(child), glm::vec3(0.0f, 0.0f, 5.0f)));
    const glm::vec3 scaleBefore = w.getScale(child);

    w.setWorldRotation(child, glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(near(w.forward(child), glm::vec3(0.0f, 0.0f, 1.0f)));
    CHECK(near(w.getWorldPosition(child), glm::vec3(0.0f, 0.0f, 5.0f)));
    CHECK(near(w.getScale(child), scaleBefore, 1.0e-3f));

    w.lookAt(child, glm::vec3(10.0f, 0.0f, 0.0f));
    CHECK(near(w.forward(child), glm::normalize(glm::vec3(10.0f, 0.0f, -5.0f)), 1.0e-3f));
    const glm::quat q = w.getWorldRotation(child);
    CHECK(near(q * glm::vec3(0.0f, 0.0f, -1.0f), w.forward(child), 1.0e-3f));

    w.rotate(NULL_ENTITY, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
    w.lookAt(NULL_ENTITY, glm::vec3(1.0f));
    CHECK(near(w.forward(NULL_ENTITY), glm::vec3(0.0f, 0.0f, -1.0f)));
}

static void testSystemScheduler() {
    Engine engine;
    std::vector<std::string> log;
    engine.addSystem(Phase::PostUpdate, "post", [&](World&, float) { log.push_back("post"); });
    engine.addSystem(Phase::Update, "update-a", [&](World&, float) { log.push_back("update-a"); });
    engine.addSystem(Phase::PreUpdate, "pre", [&](World&, float) { log.push_back("pre"); });
    engine.addSystem(Phase::Update, "update-b", [&](World&, float) { log.push_back("update-b"); });
    engine.addSystem(Phase::FixedUpdate, "fixed", [&](World&, float) { log.push_back("fixed"); });

    engine.tick(1.0f / 60.0f + 0.0001f);
    const std::vector<std::string> expected{"pre", "fixed", "update-a", "update-b", "post"};
    CHECK(log == expected);
    CHECK(engine.systemNames(Phase::Update) == (std::vector<std::string>{"animation", "update-a", "update-b"}));

    log.clear();
    CHECK(engine.setSystemEnabled("update-a", false));
    CHECK(!engine.isSystemEnabled("update-a"));
    engine.tick(1.0f / 60.0f + 0.0001f);
    CHECK(std::count(log.begin(), log.end(), "update-a") == 0);
    CHECK(std::count(log.begin(), log.end(), "update-b") == 1);

    log.clear();
    engine.addSystem(Phase::Update, "always", [&](World&, float dt) { log.push_back(dt == 0.0f ? "always-idle" : "always-run"); }, true);
    engine.setPaused(true);
    engine.tick(0.1f);
    CHECK(log == (std::vector<std::string>{"always-idle"}));
    engine.setPaused(false);

    CHECK(engine.removeSystem("update-b"));
    CHECK(!engine.removeSystem("update-b"));
    CHECK(!engine.setSystemEnabled("missing", true));

    engine.addSystem(Phase::Update, "update-a", [&](World&, float) { log.push_back("replaced"); });
    log.clear();
    engine.tick(0.01f);
    CHECK(std::count(log.begin(), log.end(), "replaced") == 1);

    Engine physicsEngine;
    World& w = physicsEngine.world();
    auto ball = w.create("ball", 1, glm::vec3(0.0f, 10.0f, 0.0f));
    w.add<SphereCollider>(ball, 0.5f);
    w.add<RigidBody>(ball);
    for (int i = 0; i < 30; ++i) physicsEngine.tick(1.0f / 60.0f);
    CHECK(w.getPosition(ball).y < 9.0f);
    physicsEngine.setSystemEnabled("physics", false);
    const float frozen = w.getPosition(ball).y;
    for (int i = 0; i < 30; ++i) physicsEngine.tick(1.0f / 60.0f);
    CHECK(w.getPosition(ball).y == frozen);
}

static void testTimers() {
    Engine engine;
    World& w = engine.world();
    int oneShot = 0, repeating = 0, cancelledRuns = 0;
    w.after(0.5f, [&](World&, EntityHandle) { ++oneShot; });
    const uint32_t ticker = w.every(0.1f, [&](World&, EntityHandle) { ++repeating; });
    const uint32_t doomed = w.after(0.3f, [&](World&, EntityHandle) { ++cancelledRuns; });
    CHECK(w.timerCount() == 3);
    CHECK(w.cancel(doomed));
    CHECK(!w.cancel(doomed));

    for (int i = 0; i < 6; ++i) engine.tick(0.1f);
    CHECK(oneShot == 1);
    CHECK(repeating >= 5 && repeating <= 6);
    CHECK(cancelledRuns == 0);
    CHECK(w.timerCount() == 1);

    engine.setTimeScale(0.5f);
    const int before = repeating;
    for (int i = 0; i < 4; ++i) engine.tick(0.1f);
    CHECK(repeating - before >= 1 && repeating - before <= 2);

    engine.setPaused(true);
    const int paused = repeating;
    for (int i = 0; i < 10; ++i) engine.tick(0.1f);
    CHECK(repeating == paused);
    engine.setPaused(false);
    engine.setTimeScale(1.0f);

    CHECK(w.cancel(ticker));
    CHECK(w.timerCount() == 0);

    auto owner = w.create("owner");
    int owned = 0;
    w.every(0.1f, [&](World&, EntityHandle e) { if (e == owner) ++owned; }, owner);
    engine.tick(0.25f);
    CHECK(owned == 2);
    w.destroy(owner);
    engine.tick(0.25f);
    CHECK(owned == 2);
    CHECK(w.timerCount() == 0);

    int chained = 0;
    w.after(0.1f, [&](World& world, EntityHandle) {
        ++chained;
        world.after(0.1f, [&](World&, EntityHandle) { ++chained; });
    });
    engine.tick(0.15f);
    CHECK(chained == 1);
    engine.tick(0.15f);
    CHECK(chained == 2);

    int selfCancel = 0;
    uint32_t selfId = 0;
    selfId = w.every(0.05f, [&](World& world, EntityHandle) {
        ++selfCancel;
        if (selfCancel == 3) world.cancel(selfId);
    });
    for (int i = 0; i < 10; ++i) engine.tick(0.05f);
    CHECK(selfCancel == 3);
    CHECK(w.timerCount() == 0);
    CHECK(w.after(1.0f, nullptr) == 0);
    CHECK(w.every(0.0f, [](World&, EntityHandle) {}) == 0);
}

static void testAnimation() {
    Engine engine;
    World& w = engine.world();
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

    auto e = w.create("e", 1);
    w.add<Animator>(e, Animator{id, 0.0f, 1.0f, true});
    step(0.5f);
    CHECK(near(w.getPosition(e), glm::vec3(5.0f, 0.0f, 0.0f)));
    CHECK(near(w.getScale(e), glm::vec3(1.5f), 1.0e-3f));
    CHECK(near(w.forward(e), glm::vec3(std::sin(glm::radians(-22.5f)), 0.0f, -std::cos(glm::radians(22.5f))), 1.0e-3f));

    step(1.0f);
    CHECK(near(w.getPosition(e), glm::vec3(10.0f, 5.0f, 0.0f)));
    step(0.25f);
    CHECK(near(w.getPosition(e), glm::vec3(10.0f, 7.5f, 0.0f)) || w.get<Animator>(e)->time < 2.0f);
    step(0.25f);
    CHECK(w.get<Animator>(e)->time >= 0.0f && w.get<Animator>(e)->time < 0.1f);
    CHECK(near(w.getPosition(e), glm::vec3(0.0f), 0.5f));

    w.get<Animator>(e)->speed = 2.0f;
    w.get<Animator>(e)->time = 0.0f;
    step(0.25f);
    CHECK(near(w.getPosition(e), glm::vec3(10.0f, 0.0f, 0.0f)) == false);
    CHECK(std::abs(w.get<Animator>(e)->time - 0.5f) < 1.0e-4f);

    AnimationClip once;
    once.name = "once";
    once.loop = false;
    once.position = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(0.0f, 4.0f, 0.0f)}};
    const uint32_t onceId = engine.animations().add(once);
    auto f = w.create("f", 1, glm::vec3(0.0f, 0.0f, 9.0f));
    w.add<Animator>(f, Animator{onceId, 0.0f, 1.0f, true});
    step(0.25f);
    step(0.25f);
    step(0.25f);
    step(0.25f);
    step(0.25f);
    CHECK(near(w.getPosition(f), glm::vec3(0.0f, 4.0f, 0.0f)));
    CHECK(!w.get<Animator>(f)->playing);
    step(0.25f);
    CHECK(near(w.getPosition(f), glm::vec3(0.0f, 4.0f, 0.0f)));

    w.get<Animator>(f)->time = 0.0f;
    w.get<Animator>(f)->playing = true;
    engine.setSystemEnabled("animation", false);
    step(0.5f);
    CHECK(near(w.getPosition(f), glm::vec3(0.0f, 4.0f, 0.0f)));
    engine.setSystemEnabled("animation", true);

    AnimationClip partial;
    partial.name = "scale-only";
    partial.scale = {{0.0f, glm::vec3(1.0f)}, {1.0f, glm::vec3(2.0f)}};
    const uint32_t partialId = engine.animations().add(partial);
    auto g = w.create("g", 1, glm::vec3(7.0f, 8.0f, 9.0f));
    w.add<Animator>(g, Animator{partialId, 0.0f, 1.0f, true});
    step(0.5f);
    CHECK(near(w.getPosition(g), glm::vec3(7.0f, 8.0f, 9.0f)));
    CHECK(near(w.getScale(g), glm::vec3(1.5f), 1.0e-3f));

    registerAnimationComponents(w);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_anim_test.litscene";
    w.get<Animator>(g)->time = 0.25f;
    w.get<Animator>(g)->speed = 3.0f;
    CHECK(w.saveScene(path));
    w.clear();
    CHECK(w.loadScene(path));
    auto rg = w.find("g");
    CHECK(w.get<Animator>(rg) && w.get<Animator>(rg)->clip == partialId && w.get<Animator>(rg)->speed == 3.0f && w.get<Animator>(rg)->time == 0.25f);
    std::filesystem::remove(path);
}

static void testPhysicsShapesAndSleep() {
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
    CHECK(hit && near(hit->normal, glm::vec3(0.0f, 0.0f, 1.0f)));
    hit = physicsRaycast(rays, glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == slab2 && std::abs(hit->distance - 19.0f) < 1.0e-4f);
    CHECK(hit && near(hit->normal, glm::vec3(0.0f, 0.0f, 1.0f)));
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

static void testJobsAndParallelSpatial() {
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

    const auto build = [](World& w) {
        w.setMeshBoundsHook([](uint32_t mesh) { return mesh == 9 ? glm::vec4(0.0f, 0.0f, 0.0f, 500.0f) : glm::vec4(0.5f, 0.0f, 0.0f, 1.0f); });
        uint32_t seed = 12345;
        const auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
        };
        std::vector<EntityHandle> made;
        for (int i = 0; i < 20000; ++i) {
            EntityHandle parent = (i % 7 == 0 && !made.empty()) ? made[made.size() / 2] : NULL_ENTITY;
            made.push_back(w.create("e", i % 500 == 0 ? 9 : 1, glm::vec3(rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f, rnd() * 200.0f - 100.0f), glm::vec3(0.5f + rnd()), parent));
            if (i % 13 == 0) w.setVisible(made.back(), false);
        }
        for (int i = 0; i < 300; ++i) w.destroy(made[static_cast<size_t>(i) * 50 % made.size()]);
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

static void testProfiler() {
    Profiler off;
    {
        ProfileScope scope(off, "ignored");
    }
    off.beginFrame();
    off.endFrame();
    CHECK(off.currentFrame().empty() && off.history().empty());

    Profiler profiler(5);
    profiler.setEnabled(true);
    for (int frame = 0; frame < 8; ++frame) {
        profiler.beginFrame();
        ProfileScope outer(profiler, "outer");
        {
            ProfileScope inner(profiler, "inner");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        {
            ProfileScope inner2(profiler, "inner");
        }
        ProfileScope quote(profiler, "with \"quote\"");
    }
    profiler.endFrame();
    CHECK(profiler.history().size() == 5);
    const auto& frame = profiler.history().back();
    CHECK(frame.size() == 4);
    CHECK(frame[0].name == "outer" && frame[0].depth == 0);
    CHECK(frame[1].name == "inner" && frame[1].depth == 1);
    CHECK(frame[2].name == "inner" && frame[2].depth == 1);
    CHECK(frame[3].depth == 1);
    CHECK(frame[1].durationMs >= 1.5);
    CHECK(frame[0].durationMs >= frame[1].durationMs);
    CHECK(frame[1].startMs >= frame[0].startMs);

    const auto top = profiler.topScopes(2);
    CHECK(top.size() == 2);
    CHECK(top[0].name == "outer");
    CHECK(top[1].name == "inner" && top[1].calls == 2);
    CHECK(profiler.topScopes(10).size() == 3 + 0 || profiler.topScopes(10).size() == 4);
    CHECK(profiler.frameIndex() == 8);

    std::ostringstream json;
    profiler.writeChromeTrace(json);
    const std::string text = json.str();
    CHECK(text.rfind("{\"traceEvents\":[", 0) == 0);
    CHECK(text.substr(text.size() - 2) == "]}");
    CHECK(std::count(text.begin(), text.end(), '{') == std::count(text.begin(), text.end(), '}'));
    CHECK(text.find("\"ph\":\"X\"") != std::string::npos);
    CHECK(text.find("with \\\"quote\\\"") != std::string::npos);
    const size_t events = 20;
    size_t counted = 0;
    for (size_t pos = text.find("\"ph\""); pos != std::string::npos; pos = text.find("\"ph\"", pos + 1)) ++counted;
    CHECK(counted == events);

    const auto path = std::filesystem::temp_directory_path() / "lit_trace_test.json";
    CHECK(profiler.writeChromeTrace(path.string()));
    CHECK(std::filesystem::file_size(path) > 100);
    std::filesystem::remove(path);
    profiler.clear();
    CHECK(profiler.history().empty() && profiler.frameIndex() == 0);

    Engine engine;
    engine.profiler().setEnabled(true);
    engine.tick(0.016f);
    engine.tick(0.016f);
    engine.tick(0.016f);
    bool sawTick = false, sawPhysics = false;
    for (const ProfileRecord& r : engine.profiler().history().back()) {
        if (r.name == "Engine::tick") sawTick = true;
        if (r.name == "physics" || r.name == "animation") sawPhysics = true;
    }
    CHECK(sawTick && sawPhysics);
}

static void testLineEditor() {
    LineEditor ed;
    CHECK(!ed.active());
    CHECK(ed.insert("x") == EditResult::None);
    CHECK(ed.press(EditKey::Backspace) == EditResult::None);

    ed.begin("hello");
    CHECK(ed.active() && ed.cursor() == 5);
    CHECK(ed.insert(" world") == EditResult::Changed);
    CHECK(ed.text() == "hello world");
    ed.press(EditKey::Home);
    CHECK(ed.cursor() == 0);
    CHECK(ed.press(EditKey::Backspace) == EditResult::None);
    ed.insert(">> ");
    CHECK(ed.text() == ">> hello world" && ed.cursor() == 3);
    ed.press(EditKey::WordRight);
    CHECK(ed.cursor() == 8);
    ed.press(EditKey::WordRight);
    CHECK(ed.cursor() == 14);
    ed.press(EditKey::WordLeft);
    CHECK(ed.cursor() == 9);
    CHECK(ed.press(EditKey::Delete) == EditResult::Changed);
    CHECK(ed.text() == ">> hello orld");
    ed.press(EditKey::End);
    CHECK(ed.press(EditKey::Delete) == EditResult::None);
    CHECK(ed.press(EditKey::Backspace) == EditResult::Changed && ed.text() == ">> hello orl");
    CHECK(ed.display() == ">> hello orl|");
    ed.press(EditKey::Left);
    ed.press(EditKey::Left);
    CHECK(ed.display() == ">> hello o|rl");
    ed.insert("\x01\t");
    CHECK(ed.text() == ">> hello orl");
    CHECK(ed.press(EditKey::Clear) == EditResult::Changed && ed.text().empty());
    CHECK(ed.press(EditKey::Clear) == EditResult::None);
    ed.insert("done");
    CHECK(ed.press(EditKey::Enter) == EditResult::Committed);
    CHECK(!ed.active() && ed.text() == "done");
    CHECK(ed.insert("more") == EditResult::None);

    ed.begin("keep");
    ed.insert("!!");
    CHECK(ed.press(EditKey::Escape) == EditResult::Cancelled);
    CHECK(!ed.active() && ed.text() == "keep");

    ed.begin("");
    ed.insert("h\xC3\xA9llo");
    CHECK(ed.text() == "h\xC3\xA9llo");
    ed.press(EditKey::Home);
    ed.press(EditKey::Right);
    ed.press(EditKey::Right);
    CHECK(ed.cursor() == 3);
    ed.press(EditKey::Backspace);
    CHECK(ed.text() == "hllo" && ed.cursor() == 1);
    ed.press(EditKey::End);
    ed.press(EditKey::Left);
    ed.press(EditKey::Left);
    ed.press(EditKey::Left);
    ed.insert("\xE2\x82\xAC");
    ed.press(EditKey::Delete);
    CHECK(ed.text() == "h\xE2\x82\xAClo");
    ed.press(EditKey::Left);
    CHECK(ed.cursor() == 1);

    ed.begin("ab", 4);
    CHECK(ed.insert("cd") == EditResult::Changed);
    CHECK(ed.insert("e") == EditResult::None);
    CHECK(ed.text() == "abcd");
    ed.begin("toolongvalue", 4);
    CHECK(ed.text() == "tool");
}

static void registerHealth(World& w) {
    w.registerComponent<Health>(
        "Health", [](const Health& h, std::ostream& out) { out << h.hp; },
        [](std::istream& in) -> std::optional<Health> {
            int hp;
            if (!(in >> hp)) return std::nullopt;
            return Health{hp};
        });
}

static void testDescribeAndEditText() {
    World w;
    registerHealth(w);
    registerLink(w);
    registerPatrol(w);
    auto a = w.create("alpha", 3, glm::vec3(1.0f));
    auto b = w.create("beta", 4, glm::vec3(0.0f), glm::vec3(1.0f), a);
    w.setTag(a, "hero");
    w.setLayer(a, 0b110);
    w.add<Health>(a, 25);
    w.add<Link>(a, Link{b});
    w.attach<Patrol>(a, 2, 1.5f);

    const EntityDescription d = w.describeEntity(a);
    CHECK(d.name == "alpha" && d.tag == "hero" && d.layer == 0b110 && d.mesh == 3 && d.visible);
    CHECK(d.childCount == 1 && d.parent.isNull());
    CHECK(d.components.size() == 2);
    CHECK(d.components[0].first == "Health" && d.components[0].second == "25");
    CHECK(d.components[1].first == "Link" && d.components[1].second == std::to_string(b.index));
    CHECK(d.scripts.size() == 1 && d.scripts[0].first == "Patrol");
    CHECK(w.describeEntity(b).parent == a);
    CHECK(w.describeEntity(NULL_ENTITY).components.empty());
    CHECK(w.componentNames() == (std::vector<std::string>{"Health", "Link"}));

    CHECK(w.setComponentFromText(a, "Health", "77"));
    CHECK(w.get<Health>(a)->hp == 77);
    CHECK(!w.setComponentFromText(a, "Health", "not a number"));
    CHECK(w.get<Health>(a)->hp == 77);
    CHECK(!w.setComponentFromText(a, "Missing", "1"));
    CHECK(w.setComponentFromText(b, "Health", "5"));
    CHECK(w.get<Health>(b)->hp == 5);
    CHECK(w.setComponentFromText(b, "Link", std::to_string(a.index)));
    CHECK(w.get<Link>(b)->target == a);
    CHECK(w.setComponentFromText(b, "Link", "999999"));
    CHECK(w.get<Link>(b)->target.isNull());

    CHECK(w.componentText(a, "Health") == std::optional<std::string>("77"));
    CHECK(!w.componentText(a, "Nope"));
    CHECK(w.removeComponentByName(b, "Health"));
    CHECK(!w.removeComponentByName(b, "Health"));
    CHECK(!w.has<Health>(b));

    History history(w);
    history.setName(a, "renamed");
    CHECK(w.getName(a) == "renamed");
    history.setName(a, "renamed");
    history.undo();
    CHECK(w.getName(a) == "alpha");
    history.redo();
    CHECK(w.getName(a) == "renamed");

    history.setTag(a, "villain");
    CHECK(w.findByTag("villain").size() == 1 && w.findByTag("hero").empty());
    history.undo();
    CHECK(w.findByTag("hero").size() == 1);

    CHECK(history.setComponentText(a, "Health", "200"));
    CHECK(w.get<Health>(a)->hp == 200);
    history.undo();
    CHECK(w.get<Health>(a)->hp == 77);
    history.redo();
    CHECK(w.get<Health>(a)->hp == 200);

    CHECK(history.setComponentText(b, "Health", "9"));
    CHECK(w.has<Health>(b));
    history.undo();
    CHECK(!w.has<Health>(b));
    history.redo();
    CHECK(w.get<Health>(b)->hp == 9);
    CHECK(!history.setComponentText(b, "Health", "oops"));
    CHECK(!history.setComponentText(b, "Nope", "1"));
}

static void testGizmoMath() {
    Ray ray{glm::vec3(3.0f, 4.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
    CHECK(std::abs(gizmo::closestParameterOnAxis(ray, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)) - 3.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::closestParameterOnAxis(ray, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)) - 4.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::closestParameterOnAxis(ray, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)) - 2.0f) < 1.0e-5f);
    CHECK(gizmo::closestParameterOnAxis(ray, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)) == 0.0f);

    const auto hit = gizmo::rayPlane(ray, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(hit && near(*hit, glm::vec3(3.0f, 4.0f, 0.0f)));
    CHECK(!gizmo::rayPlane(ray, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(!gizmo::rayPlane(Ray{glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, -1.0f)}, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

    const glm::vec3 y(0.0f, 1.0f, 0.0f);
    CHECK(std::abs(gizmo::signedAngleAroundAxis(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), y) - glm::radians(90.0f)) < 1.0e-4f);
    CHECK(std::abs(gizmo::signedAngleAroundAxis(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), y) + glm::radians(90.0f)) < 1.0e-4f);
    CHECK(std::abs(gizmo::signedAngleAroundAxis(glm::vec3(1.0f, 5.0f, 0.0f), glm::vec3(-2.0f, -3.0f, 0.0f), y) - glm::radians(180.0f)) < 1.0e-3f);

    CHECK(gizmo::snap(0.74f, 0.5f) == 0.5f && gizmo::snap(0.76f, 0.5f) == 1.0f && gizmo::snap(-0.3f, 0.5f) == -0.5f && gizmo::snap(1.234f, 0.0f) == 1.234f);

    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    const glm::mat4 rotated = gizmo::rotateAboutWorldAxis(world, glm::vec3(5.0f, 0.0f, 0.0f), y, glm::radians(90.0f));
    CHECK(near(glm::vec3(rotated[3]), glm::vec3(5.0f, 0.0f, 0.0f)));
    CHECK(near(glm::vec3(rotated[0]), glm::vec3(0.0f, 0.0f, -1.0f)));
    const glm::mat4 orbit = gizmo::rotateAboutWorldAxis(world, glm::vec3(0.0f), y, glm::radians(90.0f));
    CHECK(near(glm::vec3(orbit[3]), glm::vec3(0.0f, 0.0f, -5.0f)));

    glm::mat4 local = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) * glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), y)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    const glm::mat4 scaledX = gizmo::scaleAlongLocalAxis(local, 0, 3.0f, false);
    CHECK(std::abs(glm::length(glm::vec3(scaledX[0])) - 6.0f) < 1.0e-4f && std::abs(glm::length(glm::vec3(scaledX[1])) - 2.0f) < 1.0e-4f);
    CHECK(near(glm::vec3(scaledX[3]), glm::vec3(1.0f, 2.0f, 3.0f)));
    const glm::mat4 uniform = gizmo::scaleAlongLocalAxis(local, 1, 0.5f, true);
    CHECK(std::abs(glm::length(glm::vec3(uniform[2])) - 1.0f) < 1.0e-4f && std::abs(glm::length(glm::vec3(uniform[0])) - 1.0f) < 1.0e-4f);
    CHECK(std::abs(gizmo::distanceToSegment2D(glm::vec2(5.0f, 3.0f), glm::vec2(0.0f), glm::vec2(10.0f, 0.0f)) - 3.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::distanceToSegment2D(glm::vec2(-4.0f, 3.0f), glm::vec2(0.0f), glm::vec2(10.0f, 0.0f)) - 5.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::distanceToSegment2D(glm::vec2(2.0f, 2.0f), glm::vec2(1.0f), glm::vec2(1.0f)) - std::sqrt(2.0f)) < 1.0e-5f);
}

static void testSelectionAndGroups() {
    World w;
    auto a = w.create("a", 1);
    auto b = w.create("b", 1, glm::vec3(0.0f), glm::vec3(1.0f), a);
    auto c = w.create("c", 1, glm::vec3(0.0f), glm::vec3(1.0f), b);
    auto d = w.create("d", 1);

    Selection sel;
    CHECK(sel.empty() && sel.primary().isNull());
    sel.set(a);
    sel.add(d);
    sel.add(d);
    CHECK(sel.size() == 2 && sel.primary() == d && sel.contains(a));
    sel.toggle(a);
    CHECK(sel.size() == 1 && !sel.contains(a));
    sel.toggle(a);
    CHECK(sel.size() == 2 && sel.primary() == a);
    sel.setAll({a, b, c, d});
    CHECK(sel.size() == 4);
    sel.removeDescendantsOfSelected(w);
    CHECK(sel.size() == 2 && sel.contains(a) && sel.contains(d) && !sel.contains(b) && !sel.contains(c));
    w.destroy(d);
    sel.prune(w);
    CHECK(sel.size() == 1 && sel.primary() == a);
    sel.remove(a);
    CHECK(sel.empty());
    sel.set(NULL_ENTITY);
    CHECK(sel.empty());

    World g;
    History history(g);
    auto x = g.create("x", 1);
    auto y = g.create("y", 1);
    auto z = g.create("z", 1);
    history.beginGroup();
    history.setVisible(x, false);
    history.setVisible(y, false);
    history.beginGroup();
    history.setName(z, "zz");
    history.endGroup();
    CHECK(history.inGroup());
    history.endGroup();
    CHECK(!history.inGroup());
    CHECK(!g.isVisible(x) && !g.isVisible(y) && g.getName(z) == "zz");
    CHECK(history.undo());
    CHECK(g.isVisible(x) && g.isVisible(y) && g.getName(z) == "z");
    CHECK(!history.undo());
    CHECK(history.redo());
    CHECK(!g.isVisible(x) && !g.isVisible(y) && g.getName(z) == "zz");

    history.beginGroup();
    history.endGroup();
    CHECK(history.undo());
    CHECK(g.isVisible(x));
    CHECK(!history.canUndo());

    history.beginGroup();
    history.destroy(x);
    history.destroy(y);
    history.endGroup();
    CHECK(g.aliveCount() == 1);
    history.undo();
    CHECK(g.aliveCount() == 3);
    history.redo();
    CHECK(g.aliveCount() == 1);
    history.undo();
    history.setName(history.resolve(z), "later");
    CHECK(!history.canRedo());
    history.endGroup();
    CHECK(!history.inGroup());
}

static void testFramingAndSceneBounds() {
    const glm::vec4 one = gizmo::enclosingSphere({glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)});
    CHECK(near(glm::vec3(one), glm::vec3(1.0f, 2.0f, 3.0f)) && std::abs(one.w - 4.0f) < 1.0e-4f);
    const glm::vec4 two = gizmo::enclosingSphere({glm::vec4(-10.0f, 0.0f, 0.0f, 1.0f), glm::vec4(10.0f, 0.0f, 0.0f, 1.0f)});
    CHECK(near(glm::vec3(two), glm::vec3(0.0f)) && std::abs(two.w - 11.0f) < 1.0e-3f);
    CHECK(gizmo::enclosingSphere({}).w == 0.0f);
    const glm::vec4 nested = gizmo::enclosingSphere({glm::vec4(0.0f, 0.0f, 0.0f, 10.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
    CHECK(nested.w >= 10.0f && nested.w < 11.5f);

    const float d1 = gizmo::frameDistance(1.0f, 45.0f, 16.0f / 9.0f);
    const float d2 = gizmo::frameDistance(2.0f, 45.0f, 16.0f / 9.0f);
    CHECK(std::abs(d2 - 2.0f * d1) < 1.0e-3f);
    const float tall = gizmo::frameDistance(1.0f, 45.0f, 0.5f);
    CHECK(tall > d1);
    CHECK(std::abs(d1 - 1.1f / std::sin(glm::radians(22.5f))) < 1.0e-3f);

    World w;
    CHECK(!w.sceneBounds());
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    w.create("empty");
    CHECK(!w.sceneBounds());
    w.create("a", 1, glm::vec3(-10.0f, 0.0f, 0.0f));
    w.create("b", 1, glm::vec3(10.0f, 0.0f, 0.0f));
    const auto bounds = w.sceneBounds();
    CHECK(bounds && near(glm::vec3(*bounds), glm::vec3(0.0f)) && bounds->w > 10.0f && bounds->w < 20.0f);
}

static void testSceneVersioningAndTruncation() {
    World w;
    registerHealth(w);
    auto a = w.create("alpha", 3, glm::vec3(1.0f, 2.0f, 3.0f));
    auto b = w.create("beta", 4, glm::vec3(0.0f), glm::vec3(1.0f), a);
    w.add<Health>(b, 11);
    w.camera().setPos(glm::vec3(7.0f, 8.0f, 9.0f));
    w.camera().setOrientation(-30.0f, 12.0f);
    w.camera().setFov(70.0f);
    w.camera().setFarPlane(1234.0f);

    const std::string saved = w.snapshot();
    CHECK(saved.rfind("LITSCENE 3\n2\ncamera ", 0) == 0);
    w.camera().setPos(glm::vec3(0.0f));
    w.camera().setOrientation(-90.0f, 0.0f);
    w.camera().setFov(45.0f);
    CHECK(w.restore(saved));
    CHECK(near(w.camera().getPosition(), glm::vec3(7.0f, 8.0f, 9.0f)));
    CHECK(std::abs(w.camera().getYaw() + 30.0f) < 1.0e-4f && std::abs(w.camera().getPitch() - 12.0f) < 1.0e-4f);
    CHECK(w.camera().getFov() == 70.0f && w.camera().getFarPlane() == 1234.0f);

    w.setRestoreCameraOnLoad(false);
    w.camera().setPos(glm::vec3(1.0f));
    CHECK(w.restore(saved));
    CHECK(near(w.camera().getPosition(), glm::vec3(1.0f)));
    w.setRestoreCameraOnLoad(true);

    const std::string legacy =
        "LITSCENE 2\n2\n"
        "0 -1 1 3 0 0 1 1 0 0 0 0 1 0 0 0 0 1 0 5 6 7 1 1 1 0\tlegacy root\n"
        "1 0 1 4 0 0 1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 1 0\tlegacy child\n"
        "component Health 1\t42\n";
    World old;
    registerHealth(old);
    CHECK(old.restore(legacy));
    CHECK(old.aliveCount() == 2);
    auto lr = old.find("legacy root");
    auto lc = old.find("legacy child");
    CHECK(old.getParent(lc) == lr && near(old.getPosition(lr), glm::vec3(5.0f, 6.0f, 7.0f)));
    CHECK(old.get<Health>(lc) && old.get<Health>(lc)->hp == 42);

    const std::string legacy1 = "LITSCENE 1\n1\n0 -1 1 3 0 0 1 1 0 0 0 0 1 0 0 0 0 1 0 1 2 3 1\tv1\n";
    World v1;
    CHECK(v1.restore(legacy1) && v1.aliveCount() == 1);

    int migrated = 0;
    old.setSceneMigration([&](int fileVersion, std::string& line) {
        if (fileVersion >= World::kSceneVersion) return;
        ++migrated;
        if (line.rfind("component OldHealth ", 0) == 0) line.replace(10, 9, "Health");
    });
    const std::string oldNames = legacy + "component OldHealth 0\t7\n";
    CHECK(old.restore(oldNames));
    CHECK(migrated > 0);
    CHECK(old.get<Health>(old.find("legacy root")) && old.get<Health>(old.find("legacy root"))->hp == 7);
    old.setSceneMigration(nullptr);

    const size_t before = old.aliveCount();
    std::string truncated = saved.substr(0, saved.rfind("component"));
    truncated = truncated.substr(0, truncated.rfind('\n', truncated.size() - 2) + 1);
    World guard;
    guard.create("keep me", 1);
    CHECK(!guard.restore(truncated));
    CHECK(guard.aliveCount() == 1 && !guard.find("keep me").isNull());
    CHECK(!guard.restore("LITSCENE 3\n"));
    CHECK(!guard.restore("LITSCENE 9\n1\n0 -1 1 0 0 0 1 1\tx\n"));
    CHECK(!guard.restore("NOTASCENE 3\n0\n"));
    CHECK(!guard.restore(""));
    CHECK(guard.aliveCount() == 1);
    CHECK(guard.restore("LITSCENE 3\n0\n"));
    CHECK(guard.aliveCount() == 0);
    (void)before;
    (void)b;
}

static bool consistent(World& w) {
    size_t alive = 0;
    bool ok = true;
    std::vector<EntityHandle> all;
    w.forEach([&](EntityHandle e) {
        ++alive;
        all.push_back(e);
    });
    ok = ok && alive == w.aliveCount();
    for (EntityHandle e : all) {
        const EntityHandle p = w.getParent(e);
        if (!p.isNull()) {
            ok = ok && w.isAlive(p) && p != e;
            const auto kids = w.getChildren(p);
            ok = ok && std::find(kids.begin(), kids.end(), e) != kids.end();
        }
        size_t hops = 0;
        for (EntityHandle a = p; !a.isNull(); a = w.getParent(a)) {
            if (++hops > alive) {
                ok = false;
                break;
            }
        }
        const glm::mat4 m = w.getWorldMatrix(e);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) ok = ok && std::isfinite(m[c][r]);
        }
    }
    size_t rootCount = 0;
    for (EntityHandle r = w.firstRoot(); !r.isNull(); r = w.nextSibling(r)) {
        ++rootCount;
        ok = ok && w.getParent(r).isNull();
    }
    ok = ok && w.getRoots().size() == rootCount;
    return ok;
}

static void testLoaderHardeningAndFuzz() {
    const auto makeWorld = []() {
        auto w = std::make_unique<World>();
        registerHealth(*w);
        registerLink(*w);
        registerPatrol(*w);
        w->setMeshHooks([](uint32_t id) { return id == 1 ? std::string("cube") : (id == 2 ? std::string("sphere") : std::string()); }, [](const std::string& name) { return name == "cube" ? 1u : (name == "sphere" ? 2u : 0u); });
        return w;
    };

    auto source = makeWorld();
    auto root = source->create("root", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    auto mid = source->create("mid", 2, glm::vec3(0.0f), glm::vec3(2.0f), root);
    auto leaf = source->create("leaf name with spaces", 1, glm::vec3(4.0f), glm::vec3(1.0f), mid);
    auto other = source->create("other", 2);
    source->add<Health>(mid, 5);
    source->add<Link>(leaf, Link{root});
    source->attach<Patrol>(other, 2, 3.5f);
    source->setTag(leaf, "tagged");
    source->setLayer(leaf, 0b1010);
    source->setVisible(other, false);
    const std::string valid = source->snapshot();

    {
        auto w = makeWorld();
        std::istringstream in(valid);
        CHECK(w->loadScene(in));
        CHECK(w->aliveCount() == 4);
        CHECK(consistent(*w));
    }

    const auto rejects = [&](const std::string& text) {
        auto w = makeWorld();
        w->create("guard", 1);
        std::istringstream in(text);
        const bool ok = w->loadScene(in);
        return !ok && w->aliveCount() == 1 && !w->find("guard").isNull();
    };
    const std::string header = "LITSCENE 3\n1\n";
    const std::string matrix = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";
    CHECK(!rejects(header + "0 -1 1 1 0 0 1 " + matrix + " 1 0\tok\n"));
    CHECK(rejects(header + "0 -1 1 1 0 0 nan " + matrix + " 1 0\tbad alpha\n"));
    CHECK(rejects(header + "0 -1 1 1 0 0 1 1 0 0 0 0 1 0 0 0 0 inf 0 0 0 0 1 1 0\tinf matrix\n"));
    CHECK(rejects("LITSCENE 3\n2\n0 -1 1 1 0 0 1 " + matrix + " 1 0\ta\n0 -1 1 1 0 0 1 " + matrix + " 1 0\tduplicate id\n"));
    CHECK(rejects(header + "-5 -1 1 1 0 0 1 " + matrix + " 1 0\tnegative id\n"));
    CHECK(rejects(header + "0 -7 1 1 0 0 1 " + matrix + " 1 0\tnegative parent\n"));
    CHECK(rejects("LITSCENE 3\n99999999999\n"));
    CHECK(rejects(header + "0 -1 1 1 0 0 1 " + matrix + " 1 0\t" + std::string(5000, 'n') + "\n"));
    CHECK(rejects(header + "0 -1 1 1 0 0 1 " + matrix + " 1 0\t" + std::string(70000, 'n') + "\n"));
    CHECK(rejects("LITSCENE 3\n2\n0 -1 1 1 0 0 1 " + matrix + " 1 0\ta\n"));

    {
        auto w = makeWorld();
        std::istringstream in("LITSCENE 3\n3\n0 0 1 1 0 0 1 " + matrix + " 1 0\tself parent\n1 2 1 1 0 0 1 " + matrix + " 1 0\tcycle a\n2 1 1 1 0 0 1 " + matrix + " 1 0\tcycle b\n");
        CHECK(w->loadScene(in));
        CHECK(w->aliveCount() == 3 && consistent(*w));
    }

    uint32_t seed = 0xC0FFEEu;
    const auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    int accepted = 0, refused = 0;
    for (int iteration = 0; iteration < 3000; ++iteration) {
        std::string text = valid;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits && !text.empty(); ++e) {
            switch (next() % 6) {
                case 0: text[next() % text.size()] = static_cast<char>(next() % 256); break;
                case 1: text.resize(next() % text.size()); break;
                case 2: {
                    const size_t at = next() % text.size();
                    const size_t len = std::min<size_t>(next() % 40, text.size() - at);
                    text.erase(at, len);
                    break;
                }
                case 3: {
                    const size_t at = next() % text.size();
                    std::string junk;
                    for (uint32_t k = next() % 20; k > 0; --k) junk.push_back(static_cast<char>(32 + next() % 95));
                    text.insert(at, junk);
                    break;
                }
                case 4: {
                    const size_t from = next() % text.size();
                    const size_t to = text.find('\n', from);
                    const std::string lineText = text.substr(from, to == std::string::npos ? std::string::npos : to - from + 1);
                    text.insert(next() % text.size(), lineText);
                    break;
                }
                default: {
                    const size_t at = next() % text.size();
                    const char digits[] = "0123456789-. eEnNaAiIfF";
                    text[at] = digits[next() % (sizeof(digits) - 1)];
                    break;
                }
            }
        }
        auto w = makeWorld();
        w->create("guard", 1);
        std::istringstream in(text);
        const bool ok = w->loadScene(in);
        if (ok) {
            ++accepted;
            CHECK(consistent(*w));
        } else {
            ++refused;
            CHECK(w->aliveCount() == 1);
            CHECK(!w->find("guard").isNull());
            CHECK(consistent(*w));
        }
    }
    CHECK(accepted > 0 && refused > 0);
    std::printf("fuzz: %d accepted, %d refused\n", accepted, refused);
}

static void testSoakInvariants() {
    World w;
    registerHealth(w);
    registerLink(w);
    registerPatrol(w);
    w.setMeshHooks([](uint32_t id) { return id == 1 ? std::string("cube") : std::string(); }, [](const std::string& name) { return name == "cube" ? 1u : 0u; });
    w.setMeshBoundsHook([](uint32_t mesh) { return mesh == 3 ? glm::vec4(0.0f, 0.0f, 0.0f, 200.0f) : glm::vec4(0.5f, 0.0f, 0.0f, 1.0f); });
    History history(w);

    uint32_t seed = 987654321u;
    const auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    const auto chance = [&](uint32_t modulo) { return next() % modulo; };
    const auto pick = [&](std::vector<EntityHandle>& v) -> EntityHandle { return v.empty() ? NULL_ENTITY : v[next() % v.size()]; };
    const auto randomVec = [&]() { return glm::vec3(float(next() % 2000) / 10.0f - 100.0f, float(next() % 2000) / 10.0f - 100.0f, float(next() % 2000) / 10.0f - 100.0f); };

    const char* tags[] = {"", "red", "blue", "green"};
    std::string lastOp;
    bool failed = false;
    constexpr int kOps = 12000;
    for (int op = 0; op < kOps && !failed; ++op) {
        std::vector<EntityHandle> alive;
        w.forEach([&](EntityHandle e) { alive.push_back(e); });
        const uint32_t kind = chance(34);
        EntityHandle e = pick(alive);
        EntityHandle other = pick(alive);
        switch (kind) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                if (alive.size() < 120) {
                    lastOp = "create";
                    const uint32_t meshChoice = chance(6);
                    const uint32_t mesh = meshChoice == 0 ? NO_MESH : (meshChoice == 1 ? 3u : 1u + chance(2));
                    w.create("e" + std::to_string(op), mesh, randomVec(), glm::vec3(0.5f + float(chance(20)) / 10.0f), chance(3) == 0 ? other : NULL_ENTITY);
                }
                break;
            case 5:
                lastOp = "destroy";
                w.destroy(e);
                break;
            case 6:
                lastOp = "setParent";
                w.setParent(e, chance(4) == 0 ? NULL_ENTITY : other, chance(2) == 0);
                break;
            case 7:
                lastOp = "setPosition";
                w.setPosition(e, randomVec());
                break;
            case 8:
                lastOp = "rotate";
                w.rotate(e, randomVec() + glm::vec3(0.1f), float(chance(100)) / 30.0f);
                break;
            case 9:
                lastOp = "setScale";
                w.setScale(e, glm::vec3(0.2f + float(chance(30)) / 10.0f));
                break;
            case 10:
                lastOp = "setVisible";
                w.setVisible(e, chance(2) == 0);
                break;
            case 11:
                lastOp = "setMesh";
                w.setMesh(e, chance(4) == 0 ? NO_MESH : 1u + chance(3));
                break;
            case 12:
                lastOp = "setLayer/tag";
                w.setLayer(e, 1u << chance(4));
                w.setTag(e, tags[chance(4)]);
                w.setName(e, "n" + std::to_string(chance(50)));
                break;
            case 13:
                lastOp = "add Health";
                w.add<Health>(e, static_cast<int>(chance(100)));
                break;
            case 14:
                lastOp = "add Link";
                w.add<Link>(e, Link{other});
                break;
            case 15:
                lastOp = "remove component";
                if (chance(2) == 0) {
                    w.remove<Health>(e);
                } else {
                    w.remove<Link>(e);
                }
                break;
            case 16:
                lastOp = "script";
                if (chance(3) == 0) {
                    w.removeScripts(e);
                } else {
                    w.attach<Patrol>(e, static_cast<int>(chance(9)), 1.0f);
                }
                break;
            case 17: {
                lastOp = "destroyBatch";
                std::vector<EntityHandle> doomed;
                for (EntityHandle a : alive) {
                    if (chance(12) == 0) doomed.push_back(a);
                }
                w.destroyBatch(doomed);
                break;
            }
            case 18:
                lastOp = "compact";
                (void)w.compact();
                break;
            case 19:
                lastOp = "raycast";
                (void)w.raycast(randomVec(), randomVec());
                break;
            case 20:
                lastOp = "overlapSphere";
                (void)w.overlapSphere(randomVec(), 20.0f + float(chance(80)));
                break;
            case 21:
                lastOp = "queryFrustum";
                w.camera().setPos(randomVec());
                w.camera().setOrientation(float(chance(360)), float(chance(120)) - 60.0f);
                (void)w.queryFrustum(w.camera());
                break;
            case 22:
                lastOp = "getWorldMatrix";
                (void)w.getWorldMatrix(e);
                (void)w.getWorldPosition(other);
                break;
            case 23:
                lastOp = "history setLocal";
                history.setLocalMatrix(e, glm::translate(glm::mat4(1.0f), randomVec()));
                break;
            case 24:
                lastOp = "history destroy";
                history.destroy(e);
                break;
            case 25:
                lastOp = "history undo";
                history.undo();
                break;
            case 26:
                lastOp = "history redo";
                history.redo();
                break;
            case 27:
                lastOp = "history setParent";
                history.setParent(e, other, chance(2) == 0);
                break;
            case 28:
                lastOp = "snapshot/restore";
                if (chance(20) == 0) {
                    const std::string text = w.snapshot();
                    if (!w.restore(text)) failed = true;
                    history.clear();
                }
                break;
            case 29:
                lastOp = "animation";
                if (chance(4) == 0) {
                    std::vector<glm::vec3> base;
                    for (uint32_t i = 0; i < 1 + chance(6); ++i) base.push_back(randomVec());
                    w.setAnimation(chance(8), base);
                }
                w.setAnimationTime(float(chance(1000)) / 37.0f);
                break;
            case 30:
                lastOp = "update";
                w.update(0.016f);
                break;
            case 31:
                lastOp = "setPositions batch";
                {
                    std::vector<std::pair<EntityHandle, glm::vec3>> moves;
                    for (EntityHandle a : alive) {
                        if (chance(5) == 0) moves.emplace_back(a, randomVec());
                    }
                    w.setPositions(moves);
                }
                break;
            case 32:
                lastOp = "history component/name";
                history.setName(e, "h" + std::to_string(chance(20)));
                history.setTag(e, tags[chance(4)]);
                (void)history.setComponentText(e, "Health", std::to_string(chance(500)));
                break;
            default:
                lastOp = "instantiate prefab";
                if (alive.size() < 100 && !e.isNull()) {
                    const Prefab prefab = w.capture(e);
                    w.instantiate(prefab, chance(2) == 0 ? other : NULL_ENTITY);
                }
                break;
        }
        const std::vector<std::string> problems = w.validate();
        if (!problems.empty()) {
            std::fprintf(stderr, "soak op %d (%s) broke invariants: %s\n", op, lastOp.c_str(), problems[0].c_str());
            failed = true;
        }
    }
    CHECK(!failed);
    CHECK(w.validate().empty());
}

int main() {
    testHandles();
    testHierarchy();
    testKeepWorldTransform();
    testVisibility();
    testNames();
    testComponents();
    testScripts();
    testSaveLoad();
    testComponentSerialization();
    testMeshRemap();
    testPrefab();
    testLayersAndTags();
    testWorldCache();
    testEvents();
    testTrimAndCompact();
    testBatch();
    testScriptSerialization();
    testTimeService();
    testLights();
    testFrustumQuery();
    testPhysics();
    testPhysicsShapesAndSleep();
    testJobsAndParallelSpatial();
    testProfiler();
    testLineEditor();
    testGizmoMath();
    testSelectionAndGroups();
    testFramingAndSceneBounds();
    testSceneVersioningAndTruncation();
    testLoaderHardeningAndFuzz();
    testSoakInvariants();
    testDescribeAndEditText();
    testAdditiveLoad();
    testEntityReferences();
    testHistory();
    testSiblingIteration();
    testSnapshotRestore();
    testMeshlessEntities();
    testEachAndBuilder();
    testTransformConveniences();
    testSystemScheduler();
    testTimers();
    testAnimation();
    testAnimationAgreement();
    testCompactKeepsSpatialAndScripts();
    testFixedUpdate();
    testCameraEntity();
    testSpatial();
    testScreenRay();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
