#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

import Engine.World;
import Engine.Render.entity;
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
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
