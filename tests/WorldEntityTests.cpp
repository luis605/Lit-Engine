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

void testHandles() {
    World world;
    auto a = world.create("a", 1);
    CHECK(world.isAlive(a));
    world.destroy(a);
    CHECK(!world.isAlive(a));
    auto b = world.create("b", 1);
    CHECK(b.index == a.index);
    CHECK(b.generation != a.generation);
    CHECK(!world.isAlive(a));
    CHECK(world.isAlive(b));
    world.setPosition(a, glm::vec3(9.0f));
    CHECK(world.getPosition(b).x == 0.0f);
    CHECK(world.aliveCount() == 1);
}

void testHierarchy() {
    World world;
    auto p = world.create("p", 1);
    auto c1 = world.create("c1", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    auto c2 = world.create("c2", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    auto g = world.create("g", 1, glm::vec3(0.0f), glm::vec3(1.0f), c1);
    CHECK(world.getChildren(p).size() == 2);
    CHECK(world.getParent(g) == c1);
    CHECK(world.isDescendantOf(g, p));
    world.setParent(p, g);
    CHECK(world.getParent(p).isNull());
    world.setParent(g, c2);
    CHECK(world.getChildren(c1).empty());
    CHECK(world.getChildren(c2).size() == 1);
    CHECK(world.getRoots().size() == 1);
    world.destroy(p);
    CHECK(world.aliveCount() == 0);
    CHECK(!world.isAlive(g));
    CHECK(world.getRoots().empty());
}

void testKeepWorldTransform() {
    World world;
    auto p = world.create("p", 1, glm::vec3(10.0f, 0.0f, 0.0f));
    auto c = world.create("c", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    world.setParent(c, p, true);
    const glm::vec3 wp = world.getWorldPosition(c);
    CHECK(wp.x == 1.0f && wp.y == 2.0f && wp.z == 3.0f);
    CHECK(world.getPosition(c).x == -9.0f);
    world.setParent(c, NULL_ENTITY, true);
    CHECK(world.getPosition(c).x == 1.0f);
}

void testVisibility() {
    World world;
    auto e = world.create("e", 7);
    CHECK(world.isVisible(e));
    world.setVisible(e, false);
    CHECK(!world.isVisible(e));
    CHECK(world.getMesh(e) == 7);
    world.setMesh(e, 9);
    CHECK(world.getMesh(e) == 9);
    world.setVisible(e, true);
    CHECK(world.database().renderables[e.index].mesh_uuid == 9);
    CHECK((world.getRenderFlags(e) & RENDER_HIDDEN) == 0);
    world.setVisible(e, false);
    CHECK((world.getRenderFlags(e) & RENDER_HIDDEN) != 0);
    world.setRenderFlags(e, RENDER_NO_OCCLUDER, true);
    world.setRenderFlags(e, RENDER_HIDDEN, false);
    CHECK((world.getRenderFlags(e) & RENDER_HIDDEN) != 0);
    CHECK((world.getRenderFlags(e) & RENDER_NO_OCCLUDER) != 0);
    world.setVisible(e, true);
    CHECK(world.getRenderFlags(e) == RENDER_NO_OCCLUDER);
}

void testIncrementalRenderableTracking() {
    World world;
    auto a = world.create("a", 1);
    auto b = world.create("b", 1);
    auto& db = world.database();
    const uint64_t dataVersion = db.m_dataVersion;
    db.transformDirty.clear();
    db.renderableDirty.clear();

    world.setMaterial(a, 3);
    world.setAlpha(a, 0.5f);
    CHECK(db.m_dataVersion == dataVersion);
    CHECK(db.renderableDirty.list.size() == 1);
    CHECK(db.transformDirty.list.empty());

    world.setVisible(b, false);
    world.setMesh(b, 2);
    CHECK(db.m_dataVersion == dataVersion);
    CHECK(db.renderableDirty.list.size() == 2);
    CHECK(db.transformDirty.list.size() == 1 && db.transformDirty.list[0] == b.index);

    db.renderableDirty.clear();
    world.setShader(a, 1);
    CHECK(db.renderableDirty.list.size() == 1 && db.renderableDirty.list[0] == a.index);

    db.renderableDirty.truncate(a.index);
    CHECK(db.renderableDirty.list.empty());
    world.setShader(a, 2);
    CHECK(db.renderableDirty.list.size() == 1);
}

void testNames() {
    World world;
    auto a = world.create("same", 1);
    auto b = world.create("same", 1);
    CHECK(world.findAll("same").size() == 2);
    world.destroy(a);
    CHECK(world.find("same") == b);
    CHECK(world.find("missing").isNull());
}

void testComponents() {
    World world;
    auto a = world.create("a", 1);
    auto b = world.create("b", 1);
    auto c = world.create("c", 1);
    world.add<Health>(a, 10);
    world.add<Health>(b, 20);
    world.add<Health>(c, 30);
    int sum = 0;
    world.view<Health>([&](EntityHandle, Health& h) { sum += h.hp; });
    CHECK(sum == 60);
    world.destroy(a);
    CHECK(world.get<Health>(a) == nullptr);
    CHECK(world.get<Health>(b)->hp == 20);
    world.remove<Health>(c);
    CHECK(!world.has<Health>(c));
    auto d = world.create("d", 1);
    CHECK(!world.has<Health>(d));
}

void testScripts() {
    World world;
    int starts = 0, updates = 0, destroys = 0;
    auto e = world.create("e", 1);
    world.attach<Counter>(e, &starts, &updates, &destroys);
    world.update(0.016f);
    CHECK(starts == 1);
    CHECK(updates == 1);
    CHECK(destroys == 1);
    CHECK(!world.isAlive(e));
    world.update(0.016f);
    CHECK(updates == 1);
}

void testSiblingIteration() {
    World world;
    auto p = world.create("p", 1);
    auto r2 = world.create("r2", 1);
    world.create("c1", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    world.create("c2", 1, glm::vec3(0.0f), glm::vec3(1.0f), p);
    size_t roots = 0;
    for (EntityHandle r = world.firstRoot(); !r.isNull(); r = world.nextSibling(r)) ++roots;
    CHECK(roots == 2);
    size_t kids = 0;
    for (EntityHandle c = world.firstChild(p); !c.isNull(); c = world.nextSibling(c)) ++kids;
    CHECK(kids == 2);
    CHECK(world.firstChild(r2).isNull());
    CHECK(world.firstChild(NULL_ENTITY).isNull());
}

void testMeshlessEntities() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    auto empty = world.create("empty");
    CHECK(world.getMesh(empty) == NO_MESH);
    EntityDesc desc;
    CHECK(desc.mesh == NO_MESH);
    world.setPosition(empty, glm::vec3(0.0f, 0.0f, -5.0f));
    auto solid = world.create("solid", 1, glm::vec3(0.0f, 0.0f, -20.0f));
    auto hit = world.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(hit && hit->entity == solid);
    CHECK(world.overlapSphere(glm::vec3(0.0f, 0.0f, -5.0f), 2.0f).empty());
    CHECK(world.getWorldBounds(empty).w == 0.0f);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_meshless_test.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    CHECK(world.getMesh(world.find("empty")) == NO_MESH);
    CHECK(world.getMesh(world.find("solid")) == 1);
    std::filesystem::remove(path);

    world.setMesh(world.find("empty"), 3);
    CHECK(world.getMesh(world.find("empty")) == 3);
}

struct Tint {
    float r;
};

void testEachAndBuilder() {
    World world;
    auto a = world.spawn().name("a").mesh(1).at(glm::vec3(1.0f, 2.0f, 3.0f)).with<Health>(10).with<Tint>(0.5f).build();
    auto b = world.spawn().name("b").with<Health>(20).tag("t").layer(0b100).visible(false).build();
    auto c = world.spawn().name("c").with<Tint>(0.25f).build();
    auto d = world.spawn().name("d").mesh(2).with<Health>(30).with<Tint>(0.75f).with<Link>(a).build();
    CHECK(world.getName(a) == "a" && world.getMesh(a) == 1 && world.getPosition(a).z == 3.0f);
    CHECK(world.getTag(b) == "t" && world.getLayer(b) == 0b100 && !world.isVisible(b));

    int both = 0;
    float sum = 0.0f;
    world.each<Health, Tint>([&](EntityHandle e, Health& h, Tint& t) {
        ++both;
        sum += float(h.hp) * t.r;
        CHECK(e == a || e == d);
    });
    CHECK(both == 2);
    CHECK(std::abs(sum - (10 * 0.5f + 30 * 0.75f)) < 1.0e-5f);

    int triple = 0;
    world.each<Link, Health, Tint>([&](EntityHandle e, Link& l, Health& h, Tint& t) {
        ++triple;
        CHECK(e == d && l.target == a && h.hp == 30 && t.r == 0.75f);
    });
    CHECK(triple == 1);

    int single = 0;
    world.each<Tint>([&](EntityHandle, Tint& t) {
        ++single;
        t.r += 1.0f;
    });
    CHECK(single == 3);
    CHECK(world.get<Tint>(c)->r == 1.25f);

    int none = 0;
    world.each<Link, Tint, Health>([&](EntityHandle, Link&, Tint&, Health&) { ++none; });
    CHECK(none == 1);
    world.remove<Link>(d);
    none = 0;
    world.each<Link, Tint, Health>([&](EntityHandle, Link&, Tint&, Health&) { ++none; });
    CHECK(none == 0);

    EntityHandle implicit = world.spawn().name("implicit");
    CHECK(world.isAlive(implicit) && world.getName(implicit) == "implicit");
}

void testTransformConveniences() {
    World world;
    auto e = world.create("e", 1, glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(nearlyEqual(world.forward(e), glm::vec3(0.0f, 0.0f, -1.0f)));
    CHECK(nearlyEqual(world.right(e), glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(nearlyEqual(world.up(e), glm::vec3(0.0f, 1.0f, 0.0f)));

    world.rotate(e, glm::vec3(0.0f, 1.0f, 0.0f), glm::radians(90.0f));
    CHECK(nearlyEqual(world.forward(e), glm::vec3(-1.0f, 0.0f, 0.0f)));
    CHECK(nearlyEqual(world.right(e), glm::vec3(0.0f, 0.0f, -1.0f)));

    world.lookAt(e, glm::vec3(1.0f, 2.0f, -10.0f));
    CHECK(nearlyEqual(world.forward(e), glm::vec3(0.0f, 0.0f, -1.0f)));
    CHECK(nearlyEqual(world.getWorldPosition(e), glm::vec3(1.0f, 2.0f, 3.0f)));
    world.lookAt(e, glm::vec3(11.0f, 2.0f, 3.0f));
    CHECK(nearlyEqual(world.forward(e), glm::vec3(1.0f, 0.0f, 0.0f)));
    world.lookAt(e, glm::vec3(1.0f, 12.0f, 3.0f));
    CHECK(nearlyEqual(world.forward(e), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(glm::length(world.up(e)) > 0.99f);

    auto parent = world.create("parent", 0, glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(2.0f));
    world.setRotation(parent, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
    auto child = world.create("child", 1, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), parent);
    CHECK(nearlyEqual(world.getWorldPosition(child), glm::vec3(10.0f, 2.0f, 0.0f)));
    CHECK(nearlyEqual(world.right(child), glm::vec3(0.0f, 1.0f, 0.0f)));

    world.setWorldPosition(child, glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK(nearlyEqual(world.getWorldPosition(child), glm::vec3(0.0f, 0.0f, 5.0f)));
    const glm::vec3 scaleBefore = world.getScale(child);

    world.setWorldRotation(child, glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(nearlyEqual(world.forward(child), glm::vec3(0.0f, 0.0f, 1.0f)));
    CHECK(nearlyEqual(world.getWorldPosition(child), glm::vec3(0.0f, 0.0f, 5.0f)));
    CHECK(nearlyEqual(world.getScale(child), scaleBefore, 1.0e-3f));

    world.lookAt(child, glm::vec3(10.0f, 0.0f, 0.0f));
    CHECK(nearlyEqual(world.forward(child), glm::normalize(glm::vec3(10.0f, 0.0f, -5.0f)), 1.0e-3f));
    const glm::quat q = world.getWorldRotation(child);
    CHECK(nearlyEqual(q * glm::vec3(0.0f, 0.0f, -1.0f), world.forward(child), 1.0e-3f));

    world.rotate(NULL_ENTITY, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);
    world.lookAt(NULL_ENTITY, glm::vec3(1.0f));
    CHECK(nearlyEqual(world.forward(NULL_ENTITY), glm::vec3(0.0f, 0.0f, -1.0f)));
}

void testCameraEntity() {
    World world;
    auto cam = world.create("cam", 0, glm::vec3(3.0f, 4.0f, 5.0f));
    world.add<CameraComponent>(cam, 60.0f, 0.5f, 250.0f);
    world.setActiveCamera(cam);
    world.syncCamera();
    CHECK(world.camera().getPosition().x == 3.0f && world.camera().getPosition().z == 5.0f);
    CHECK(std::abs(world.camera().getYaw() + 90.0f) < 0.01f);
    CHECK(std::abs(world.camera().getPitch()) < 0.01f);
    CHECK(world.camera().getFov() == 60.0f);
    CHECK(world.camera().getFarPlane() == 250.0f);
    world.setRotation(cam, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    world.syncCamera();
    CHECK(std::abs(world.camera().getYaw() - 180.0f) < 0.01f || std::abs(world.camera().getYaw() + 180.0f) < 0.01f);
    world.destroy(cam);
    CHECK(world.getActiveCamera().isNull());
}

void testLights() {
    World world;
    LightSet lights;
    CHECK(!collectLights(world, glm::vec3(0.0f), lights));

    auto sun = world.create("sun");
    world.add<LightComponent>(sun, LightComponent{LightComponent::Type::Directional, glm::vec3(1.0f, 0.5f, 0.25f), 2.0f, 0.0f, 0.5f});
    world.setRotation(sun, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    auto far = world.create("far", NO_MESH, glm::vec3(100.0f, 0.0f, 0.0f));
    auto mid = world.create("mid", NO_MESH, glm::vec3(10.0f, 0.0f, 0.0f));
    auto closest = world.create("near", NO_MESH, glm::vec3(1.0f, 0.0f, 0.0f));
    world.add<LightComponent>(far, LightComponent{LightComponent::Type::Point, glm::vec3(1.0f), 1.0f, 50.0f, 1.0f});
    world.add<LightComponent>(mid, LightComponent{LightComponent::Type::Point, glm::vec3(0.0f, 1.0f, 0.0f), 3.0f, 40.0f, 1.0f});
    world.add<LightComponent>(closest, LightComponent{LightComponent::Type::Point, glm::vec3(0.0f, 0.0f, 1.0f), 4.0f, 30.0f, 1.0f});

    CHECK(collectLights(world, glm::vec3(0.0f), lights));
    CHECK(std::abs(lights.directional[0].y - 1.0f) < 1.0e-4f && std::abs(lights.directional[0].x) < 1.0e-4f);
    CHECK(lights.directional[0].w == 0.5f);
    CHECK(lights.directional[1].x == 2.0f && lights.directional[1].y == 1.0f && lights.directional[1].z == 0.5f);
    CHECK(lights.count == 3 && lights.packed.size() == 12);
    CHECK(lights.packed[0].x == 1.0f && lights.packed[0].w == 30.0f && lights.packed[1].w == 4.0f && lights.packed[1].z == 1.0f);
    CHECK(lights.packed[3].y == 0.0f);
    CHECK(lights.packed[4].x == 10.0f && lights.packed[5].y == 1.0f);
    CHECK(lights.packed[8].x == 100.0f);

    world.destroy(closest);
    world.destroy(mid);
    CHECK(collectLights(world, glm::vec3(0.0f), lights));
    CHECK(lights.count == 1 && lights.packed[0].x == 100.0f);

    World spots;
    auto lamp = spots.create("lamp", NO_MESH, glm::vec3(0.0f, 5.0f, 0.0f));
    spots.setRotation(lamp, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    spots.add<LightComponent>(lamp, LightComponent{LightComponent::Type::Spot, glm::vec3(1.0f), 2.0f, 20.0f, 1.0f, 10.0f, 40.0f});
    CHECK(collectLights(spots, glm::vec3(0.0f), lights));
    CHECK(lights.count == 1 && lights.packed[3].y == 1.0f);
    CHECK(nearlyEqual(glm::vec3(lights.packed[2]), glm::vec3(0.0f, -1.0f, 0.0f)));
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

void testEntityReferences() {
    World world;
    registerLink(world);
    auto pad = world.create("pad", 0);
    auto a = world.create("a", 1);
    auto b = world.create("b", 1, glm::vec3(0.0f), glm::vec3(1.0f), a);
    world.add<Link>(a, Link{b});
    world.add<Link>(b, Link{a});
    world.destroy(pad);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_link_test.litscene";
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    auto ra = world.find("a");
    auto rb = world.find("b");
    CHECK(world.get<Link>(ra) && world.get<Link>(ra)->target == rb);
    CHECK(world.get<Link>(rb) && world.get<Link>(rb)->target == ra);
    std::filesystem::remove(path);

    const Prefab prefab = world.capture(ra);
    auto copy = world.instantiate(prefab);
    auto copyChildren = world.getChildren(copy);
    CHECK(copyChildren.size() == 1);
    CHECK(world.get<Link>(copy) && world.get<Link>(copy)->target == copyChildren[0]);
    CHECK(world.get<Link>(copyChildren[0]) && world.get<Link>(copyChildren[0])->target == copy);
    CHECK(world.get<Link>(ra)->target == rb);
}

void testLayersAndTags() {
    World world;
    auto a = world.create("a", 1);
    auto b = world.create("b", 1);
    auto c = world.create("c", 1);
    world.setLayer(b, 0b10);
    world.setLayer(c, 0b110);
    int hits = 0;
    world.forEachInLayer(0b10, [&](EntityHandle) { ++hits; });
    CHECK(hits == 2);
    world.setRenderFlags(c, RENDER_NO_OCCLUDER, true);
    world.setTag(a, "enemy");
    world.setTag(b, "enemy");
    CHECK(world.findByTag("enemy").size() == 2);
    world.setTag(a, "ally");
    CHECK(world.findByTag("enemy").size() == 1);
    world.destroy(b);
    CHECK(world.findByTag("enemy").empty());
    CHECK(world.getTag(a) == "ally");
    const Prefab prefab = world.capture(a);
    auto copy = world.instantiate(prefab);
    CHECK(world.getTag(copy) == "ally");
    CHECK(world.findByTag("ally").size() == 2);

    const auto path = std::filesystem::temp_directory_path() / "lit_world_tag_test.litscene";
    world.setLayer(c, 0b1000);
    CHECK(world.saveScene(path));
    world.clear();
    CHECK(world.loadScene(path));
    CHECK(world.findByTag("ally").size() == 2);
    CHECK(world.getLayer(world.find("c")) == 0b1000);
    CHECK(world.getRenderFlags(world.find("c")) == RENDER_NO_OCCLUDER);
    std::filesystem::remove(path);
}

void testWorldCache() {
    World world;
    auto a = world.create("a", 1, glm::vec3(1.0f, 0.0f, 0.0f));
    auto b = world.create("b", 1, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), a);
    auto c = world.create("c", 1, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f), b);
    CHECK(world.getWorldPosition(c).x == 1.0f && world.getWorldPosition(c).y == 1.0f && world.getWorldPosition(c).z == 1.0f);
    world.setPosition(a, glm::vec3(10.0f, 0.0f, 0.0f));
    CHECK(world.getWorldPosition(c).x == 10.0f);
    world.setPosition(b, glm::vec3(0.0f, 5.0f, 0.0f));
    CHECK(world.getWorldPosition(c).y == 5.0f);
    auto d = world.create("d", 1, glm::vec3(100.0f, 0.0f, 0.0f));
    world.setParent(c, d, false);
    CHECK(world.getWorldPosition(c).x == 100.0f);
    world.setPosition(d, glm::vec3(200.0f, 0.0f, 0.0f));
    CHECK(world.getWorldPosition(c).x == 200.0f);
    world.destroy(d);
    auto e = world.create("e", 1, glm::vec3(7.0f, 0.0f, 0.0f));
    CHECK(world.getWorldPosition(e).x == 7.0f);
}

}

void runEntityTests() {
    testHandles();
    testHierarchy();
    testKeepWorldTransform();
    testVisibility();
    testIncrementalRenderableTracking();
    testNames();
    testComponents();
    testScripts();
    testLayersAndTags();
    testWorldCache();
    testLights();
    testEntityReferences();
    testSiblingIteration();
    testMeshlessEntities();
    testEachAndBuilder();
    testTransformConveniences();
    testCameraEntity();
}
