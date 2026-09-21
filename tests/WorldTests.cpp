#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <string>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.History;
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
    std::array<glm::vec4, 6> lights{};
    CHECK(!collectLights(w, glm::vec3(0.0f), lights));

    auto sun = w.create("sun", 0);
    w.add<LightComponent>(sun, LightComponent{LightComponent::Type::Directional, glm::vec3(1.0f, 0.5f, 0.25f), 2.0f, 0.0f, 0.5f});
    w.setRotation(sun, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    auto far = w.create("far", 0, glm::vec3(100.0f, 0.0f, 0.0f));
    auto mid = w.create("mid", 0, glm::vec3(10.0f, 0.0f, 0.0f));
    auto near = w.create("near", 0, glm::vec3(1.0f, 0.0f, 0.0f));
    w.add<LightComponent>(far, LightComponent{LightComponent::Type::Point, glm::vec3(1.0f), 1.0f, 50.0f, 1.0f});
    w.add<LightComponent>(mid, LightComponent{LightComponent::Type::Point, glm::vec3(0.0f, 1.0f, 0.0f), 3.0f, 40.0f, 1.0f});
    w.add<LightComponent>(near, LightComponent{LightComponent::Type::Point, glm::vec3(0.0f, 0.0f, 1.0f), 4.0f, 30.0f, 1.0f});

    CHECK(collectLights(w, glm::vec3(0.0f), lights));
    CHECK(std::abs(lights[0].y - 1.0f) < 1.0e-4f && std::abs(lights[0].x) < 1.0e-4f);
    CHECK(lights[0].w == 0.5f);
    CHECK(lights[1].x == 2.0f && lights[1].y == 1.0f && lights[1].z == 0.5f);
    CHECK(lights[2].x == 1.0f && lights[2].w == 30.0f && lights[3].w == 4.0f && lights[3].z == 1.0f);
    CHECK(lights[4].x == 10.0f && lights[5].y == 1.0f);

    w.destroy(near);
    w.destroy(mid);
    CHECK(collectLights(w, glm::vec3(0.0f), lights));
    CHECK(lights[2].x == 100.0f);
    CHECK(lights[4].w == 1.0f && lights[5].w == 0.0f);
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

static bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1.0e-4f) { return glm::length(a - b) < eps; }

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
    testAdditiveLoad();
    testEntityReferences();
    testHistory();
    testSiblingIteration();
    testSnapshotRestore();
    testMeshlessEntities();
    testEachAndBuilder();
    testTransformConveniences();
    testAnimationAgreement();
    testCompactKeepsSpatialAndScripts();
    testFixedUpdate();
    testCameraEntity();
    testSpatial();
    testScreenRay();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
