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

void testSaveLoad() {
    World world;
    auto a = world.create("root node", 3, glm::vec3(1.0f, 2.0f, 3.0f));
    auto b = world.create("leaf", 4, glm::vec3(4.0f, 5.0f, 6.0f), glm::vec3(2.0f), a);
    world.setVisible(b, false);
    world.setAlpha(b, 0.5f);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_test.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(!world.isAlive(a));
    CHECK(world.loadScene(path));
    CHECK(world.aliveCount() == 2);
    auto ra = world.find("root node");
    auto rb = world.find("leaf");
    CHECK(!ra.isNull() && !rb.isNull());
    CHECK(world.getParent(rb) == ra);
    CHECK(!world.isVisible(rb));
    CHECK(world.getMesh(rb) == 4);
    CHECK(world.getRenderable(rb).alpha == 0.5f);
    CHECK(world.getPosition(ra).z == 3.0f);
    CHECK(!world.isAlive(a));
    CHECK(!world.isAlive(b));
    std::filesystem::remove(path);
}

void testComponentSerialization() {
    World world;
    world.registerComponent<Health>(
        "Health", [](const Health& h, std::ostream& out) { out << h.hp; },
        [](std::istream& in) -> std::optional<Health> {
            int hp;
            if (!(in >> hp)) return std::nullopt;
            return Health{hp};
        });
    world.create("a", 1);
    auto b = world.create("b", 1);
    world.add<Health>(b, 42);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_comp_test.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    auto rb = world.find("b");
    CHECK(world.get<Health>(rb) && world.get<Health>(rb)->hp == 42);
    CHECK(!world.has<Health>(world.find("a")));
    std::filesystem::remove(path);
}

void testMeshRemap() {
    World world;
    world.setMeshHooks(
        [](uint32_t id) { return id == 5 ? std::string("cube") : std::string(); },
        [](const std::string& name) { return name == "cube" ? 77u : 0u; });
    world.create("a", 5);
    world.create("b", 9);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_mesh_test.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    CHECK(world.getMesh(world.find("a")) == 77);
    CHECK(world.getMesh(world.find("b")) == 9);
    std::filesystem::remove(path);
}

void testPrefab() {
    World world;
    world.registerComponent<Health>(
        "Health", [](const Health& h, std::ostream& out) { out << h.hp; },
        [](std::istream& in) -> std::optional<Health> {
            int hp;
            if (!(in >> hp)) return std::nullopt;
            return Health{hp};
        });
    auto root = world.create("root", 1, glm::vec3(1.0f, 0.0f, 0.0f));
    auto child = world.create("child", 2, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), root);
    world.add<Health>(child, 5);
    world.setVisible(child, false);
    const Prefab prefab = world.capture(root);
    CHECK(prefab.nodes.size() == 2);

    auto holder = world.create("holder", 0, glm::vec3(100.0f, 0.0f, 0.0f));
    auto copy = world.instantiate(prefab, holder);
    CHECK(world.isAlive(copy));
    CHECK(world.getParent(copy) == holder);
    CHECK(world.aliveCount() == 5);
    auto copyChildren = world.getChildren(copy);
    CHECK(copyChildren.size() == 1);
    CHECK(world.getName(copyChildren[0]) == "child");
    CHECK(!world.isVisible(copyChildren[0]));
    CHECK(world.get<Health>(copyChildren[0]) && world.get<Health>(copyChildren[0])->hp == 5);
    CHECK(world.getWorldPosition(copy).x == 101.0f);
    CHECK(world.capture(NULL_ENTITY).empty());
}

void testScriptSerialization() {
    World world;
    registerPatrol(world);
    auto a = world.create("guard", 1);
    auto b = world.create("plain", 1);
    world.attach<Patrol>(a, 3, 2.5f);
    int s = 0, u = 0, d = 0;
    world.attach<Counter>(b, &s, &u, &d);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_script_test.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    auto ra = world.find("guard");
    auto* patrol = world.getScript<Patrol>(ra);
    CHECK(patrol && patrol->waypoint == 3 && patrol->speed == 2.5f);
    CHECK(world.getScript<Counter>(world.find("plain")) == nullptr);
    std::filesystem::remove(path);

    const Prefab prefab = world.capture(ra);
    CHECK(prefab.nodes.size() == 1 && prefab.nodes[0].scripts.size() == 1);
    auto copy = world.instantiate(prefab);
    auto* copied = world.getScript<Patrol>(copy);
    CHECK(copied && copied != patrol && copied->waypoint == 3);
}

void testAdditiveLoad() {
    World world;
    auto root = world.create("root", 1, glm::vec3(1.0f, 0.0f, 0.0f));
    world.create("leaf", 2, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), root);
    const auto path = std::filesystem::temp_directory_path() / "lit_world_additive_test.litscene";
    CHECK(world.saveScene(path));

    auto anchor = world.create("anchor", 0, glm::vec3(100.0f, 0.0f, 0.0f));
    const auto first = world.loadSceneAdditive(path, anchor);
    CHECK(first && first->size() == 1);
    CHECK(world.aliveCount() == 5);
    CHECK(first && world.getParent((*first)[0]) == anchor);

    const auto second = world.loadSceneAdditive(path);
    CHECK(second && second->size() == 1);
    CHECK(world.aliveCount() == 7);
    CHECK(second && world.getParent((*second)[0]).isNull());
    CHECK(world.findAll("leaf").size() == 3);

    world.unloadGroup(*first);
    CHECK(world.aliveCount() == 5);
    CHECK(world.isAlive(anchor));
    CHECK(world.findAll("leaf").size() == 2);
    CHECK(!world.loadSceneAdditive(path.string() + ".missing"));
    CHECK(world.aliveCount() == 5);
    std::filesystem::remove(path);
}

void testSnapshotRestore() {
    World world;
    registerLink(world);
    auto a = world.create("a", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    auto b = world.create("b", 1, glm::vec3(0.0f), glm::vec3(1.0f), a);
    world.add<Link>(b, Link{a});
    world.setTag(b, "tagged");
    const std::string saved = world.snapshot();
    CHECK(!saved.empty());

    world.setPosition(a, glm::vec3(50.0f));
    world.destroy(b);
    world.create("extra", 1);
    CHECK(world.aliveCount() == 2);

    CHECK(world.restore(saved));
    CHECK(world.aliveCount() == 2);
    auto ra = world.find("a");
    auto rb = world.find("b");
    CHECK(world.getPosition(ra).x == 1.0f && world.getPosition(ra).z == 3.0f);
    CHECK(world.getParent(rb) == ra);
    CHECK(world.get<Link>(rb) && world.get<Link>(rb)->target == ra);
    CHECK(world.find("extra").isNull());
    CHECK(!world.isAlive(a));
    CHECK(!world.restore("not a scene"));
}

void testSceneSections() {
    std::vector<std::string> received;
    int begins = 0;
    auto install = [&](World& world) {
        world.addSceneSection(
            "note", [](std::ostream& out) { out << "note first line\nnote second\tvalue\n"; }, [&](std::string_view line) { received.emplace_back(line); }, [&] { ++begins; });
    };
    World source;
    install(source);
    source.create("a", 0);
    std::stringstream saved;
    CHECK(source.saveScene(saved));
    World target;
    install(target);
    CHECK(target.loadScene(saved));
    CHECK(target.aliveCount() == 1);
    CHECK(begins == 1);
    CHECK(received.size() == 2);
    CHECK(received.size() == 2 && received[0] == "first line" && received[1] == "second\tvalue");
    received.clear();
    CHECK(target.restore(source.snapshot()));
    CHECK(received.size() == 2);
    World plain;
    std::stringstream again(source.snapshot());
    CHECK(plain.loadScene(again));
    CHECK(plain.aliveCount() == 1);
}

void testBinarySnapshot() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 0.5f); });
    auto root = world.create("root", 0, glm::vec3(1, 2, 3));
    auto child = world.create("child", 1, glm::vec3(0, 1, 0), glm::vec3(2.0f), root);
    auto loner = world.create("loner", 0);
    world.setTag(child, "enemy");
    world.setVisible(loner, false);
    const WorldSnapshot snap = world.captureSnapshot();
    world.destroy(root);
    world.destroy(loner);
    auto extra = world.create("extra", 0);
    world.setPosition(extra, glm::vec3(9.0f));
    world.restoreSnapshot(snap);
    CHECK(world.aliveCount() == 3);
    CHECK(world.isAlive(root) && world.isAlive(child) && world.isAlive(loner));
    CHECK(world.getParent(child) == root);
    CHECK(world.getTag(child) == "enemy");
    CHECK(world.findByTag("enemy").size() == 1);
    CHECK(!world.isVisible(loner));
    CHECK(glm::length(world.getWorldPosition(child) - glm::vec3(1, 3, 3)) < 1e-4f);
    auto hit = world.raycast(glm::vec3(1, 3, 10), glm::vec3(0, 0, -1));
    CHECK(hit && hit->entity == child);
    CHECK(world.validate().empty());
    auto fresh = world.create("fresh", 0);
    CHECK(world.isAlive(fresh) && world.aliveCount() == 4);
    CHECK(!world.isAlive(extra) || extra.index == fresh.index);
}

void testDescribeAndEditText() {
    World world;
    registerHealth(world);
    registerLink(world);
    registerPatrol(world);
    auto a = world.create("alpha", 3, glm::vec3(1.0f));
    auto b = world.create("beta", 4, glm::vec3(0.0f), glm::vec3(1.0f), a);
    world.setTag(a, "hero");
    world.setLayer(a, 0b110);
    world.add<Health>(a, 25);
    world.add<Link>(a, Link{b});
    world.attach<Patrol>(a, 2, 1.5f);

    const EntityDescription d = world.describeEntity(a);
    CHECK(d.name == "alpha" && d.tag == "hero" && d.layer == 0b110 && d.mesh == 3 && d.visible);
    CHECK(d.childCount == 1 && d.parent.isNull());
    CHECK(d.components.size() == 2);
    CHECK(d.components[0].first == "Health" && d.components[0].second == "25");
    CHECK(d.components[1].first == "Link" && d.components[1].second == std::to_string(b.index));
    CHECK(d.scripts.size() == 1 && d.scripts[0].first == "Patrol");
    CHECK(world.describeEntity(b).parent == a);
    CHECK(world.describeEntity(NULL_ENTITY).components.empty());
    CHECK(world.componentNames() == (std::vector<std::string>{"Health", "Link"}));

    CHECK(world.setComponentFromText(a, "Health", "77"));
    CHECK(world.get<Health>(a)->hp == 77);
    CHECK(!world.setComponentFromText(a, "Health", "not a number"));
    CHECK(world.get<Health>(a)->hp == 77);
    CHECK(!world.setComponentFromText(a, "Missing", "1"));
    CHECK(world.setComponentFromText(b, "Health", "5"));
    CHECK(world.get<Health>(b)->hp == 5);
    CHECK(world.setComponentFromText(b, "Link", std::to_string(a.index)));
    CHECK(world.get<Link>(b)->target == a);
    CHECK(world.setComponentFromText(b, "Link", "999999"));
    CHECK(world.get<Link>(b)->target.isNull());

    CHECK(world.componentText(a, "Health") == std::optional<std::string>("77"));
    CHECK(!world.componentText(a, "Nope"));
    CHECK(world.removeComponentByName(b, "Health"));
    CHECK(!world.removeComponentByName(b, "Health"));
    CHECK(!world.has<Health>(b));

    History history(world);
    history.setName(a, "renamed");
    CHECK(world.getName(a) == "renamed");
    history.setName(a, "renamed");
    history.undo();
    CHECK(world.getName(a) == "alpha");
    history.redo();
    CHECK(world.getName(a) == "renamed");

    history.setTag(a, "villain");
    CHECK(world.findByTag("villain").size() == 1 && world.findByTag("hero").empty());
    history.undo();
    CHECK(world.findByTag("hero").size() == 1);

    CHECK(history.setComponentText(a, "Health", "200"));
    CHECK(world.get<Health>(a)->hp == 200);
    history.undo();
    CHECK(world.get<Health>(a)->hp == 77);
    history.redo();
    CHECK(world.get<Health>(a)->hp == 200);

    CHECK(history.setComponentText(b, "Health", "9"));
    CHECK(world.has<Health>(b));
    history.undo();
    CHECK(!world.has<Health>(b));
    history.redo();
    CHECK(world.get<Health>(b)->hp == 9);
    CHECK(!history.setComponentText(b, "Health", "oops"));
    CHECK(!history.setComponentText(b, "Nope", "1"));
}

}

void runSerializationTests() {
    testSceneSections();
    testBinarySnapshot();
    testSaveLoad();
    testComponentSerialization();
    testMeshRemap();
    testPrefab();
    testScriptSerialization();
    testDescribeAndEditText();
    testAdditiveLoad();
    testSnapshotRestore();
}
