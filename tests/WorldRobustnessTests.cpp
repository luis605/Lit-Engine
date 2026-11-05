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

bool consistent(World& world) {
    size_t alive = 0;
    bool ok = true;
    std::vector<EntityHandle> all;
    world.forEach([&](EntityHandle e) {
        ++alive;
        all.push_back(e);
    });
    ok = ok && alive == world.aliveCount();
    for (EntityHandle e : all) {
        const EntityHandle p = world.getParent(e);
        if (!p.isNull()) {
            ok = ok && world.isAlive(p) && p != e;
            const auto kids = world.getChildren(p);
            ok = ok && std::find(kids.begin(), kids.end(), e) != kids.end();
        }
        size_t hops = 0;
        for (EntityHandle a = p; !a.isNull(); a = world.getParent(a)) {
            if (++hops > alive) {
                ok = false;
                break;
            }
        }
        const glm::mat4 m = world.getWorldMatrix(e);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) ok = ok && std::isfinite(m[c][r]);
        }
    }
    size_t rootCount = 0;
    for (EntityHandle r = world.firstRoot(); !r.isNull(); r = world.nextSibling(r)) {
        ++rootCount;
        ok = ok && world.getParent(r).isNull();
    }
    ok = ok && world.getRoots().size() == rootCount;
    return ok;
}

void testSceneVersioningAndTruncation() {
    World world;
    registerHealth(world);
    auto a = world.create("alpha", 3, glm::vec3(1.0f, 2.0f, 3.0f));
    auto b = world.create("beta", 4, glm::vec3(0.0f), glm::vec3(1.0f), a);
    world.add<Health>(b, 11);
    world.camera().setPos(glm::vec3(7.0f, 8.0f, 9.0f));
    world.camera().setOrientation(-30.0f, 12.0f);
    world.camera().setFov(70.0f);
    world.camera().setFarPlane(1234.0f);

    const std::string saved = world.snapshot();
    CHECK(saved.rfind("LITSCENE 3\n2\ncamera ", 0) == 0);
    world.camera().setPos(glm::vec3(0.0f));
    world.camera().setOrientation(-90.0f, 0.0f);
    world.camera().setFov(45.0f);
    CHECK(world.restore(saved));
    CHECK(nearlyEqual(world.camera().getPosition(), glm::vec3(7.0f, 8.0f, 9.0f)));
    CHECK(std::abs(world.camera().getYaw() + 30.0f) < 1.0e-4f && std::abs(world.camera().getPitch() - 12.0f) < 1.0e-4f);
    CHECK(world.camera().getFov() == 70.0f && world.camera().getFarPlane() == 1234.0f);

    world.setRestoreCameraOnLoad(false);
    world.camera().setPos(glm::vec3(1.0f));
    CHECK(world.restore(saved));
    CHECK(nearlyEqual(world.camera().getPosition(), glm::vec3(1.0f)));
    world.setRestoreCameraOnLoad(true);

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
    CHECK(old.getParent(lc) == lr && nearlyEqual(old.getPosition(lr), glm::vec3(5.0f, 6.0f, 7.0f)));
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
}

void testLoaderHardeningAndFuzz() {
    const auto makeWorld = []() {
        auto world = std::make_unique<World>();
        registerHealth(*world);
        registerLink(*world);
        registerPatrol(*world);
        world->setMeshHooks([](uint32_t id) { return id == 1 ? std::string("cube") : (id == 2 ? std::string("sphere") : std::string()); }, [](const std::string& name) { return name == "cube" ? 1u : (name == "sphere" ? 2u : 0u); });
        return world;
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
        auto world = makeWorld();
        std::istringstream in(valid);
        CHECK(world->loadScene(in));
        CHECK(world->aliveCount() == 4);
        CHECK(consistent(*world));
    }

    const auto rejects = [&](const std::string& text) {
        auto world = makeWorld();
        world->create("guard", 1);
        std::istringstream in(text);
        const bool ok = world->loadScene(in);
        return !ok && world->aliveCount() == 1 && !world->find("guard").isNull();
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
        auto world = makeWorld();
        std::istringstream in("LITSCENE 3\n3\n0 0 1 1 0 0 1 " + matrix + " 1 0\tself parent\n1 2 1 1 0 0 1 " + matrix + " 1 0\tcycle a\n2 1 1 1 0 0 1 " + matrix + " 1 0\tcycle b\n");
        CHECK(world->loadScene(in));
        CHECK(world->aliveCount() == 3 && consistent(*world));
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
        auto world = makeWorld();
        world->create("guard", 1);
        std::istringstream in(text);
        const bool ok = world->loadScene(in);
        if (ok) {
            ++accepted;
            CHECK(consistent(*world));
        } else {
            ++refused;
            CHECK(world->aliveCount() == 1);
            CHECK(!world->find("guard").isNull());
            CHECK(consistent(*world));
        }
    }
    CHECK(accepted > 0 && refused > 0);
    std::printf("fuzz: %d accepted, %d refused\n", accepted, refused);
}

void testSoakInvariants() {
    World world;
    registerHealth(world);
    registerLink(world);
    registerPatrol(world);
    world.setMeshHooks([](uint32_t id) { return id == 1 ? std::string("cube") : std::string(); }, [](const std::string& name) { return name == "cube" ? 1u : 0u; });
    world.setMeshBoundsHook([](uint32_t mesh) { return mesh == 3 ? glm::vec4(0.0f, 0.0f, 0.0f, 200.0f) : glm::vec4(0.5f, 0.0f, 0.0f, 1.0f); });
    History history(world);

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
        world.forEach([&](EntityHandle e) { alive.push_back(e); });
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
                    world.create("e" + std::to_string(op), mesh, randomVec(), glm::vec3(0.5f + float(chance(20)) / 10.0f), chance(3) == 0 ? other : NULL_ENTITY);
                }
                break;
            case 5:
                lastOp = "destroy";
                world.destroy(e);
                break;
            case 6:
                lastOp = "setParent";
                world.setParent(e, chance(4) == 0 ? NULL_ENTITY : other, chance(2) == 0);
                break;
            case 7:
                lastOp = "setPosition";
                world.setPosition(e, randomVec());
                break;
            case 8:
                lastOp = "rotate";
                world.rotate(e, randomVec() + glm::vec3(0.1f), float(chance(100)) / 30.0f);
                break;
            case 9:
                lastOp = "setScale";
                world.setScale(e, glm::vec3(0.2f + float(chance(30)) / 10.0f));
                break;
            case 10:
                lastOp = "setVisible";
                world.setVisible(e, chance(2) == 0);
                break;
            case 11:
                lastOp = "setMesh";
                world.setMesh(e, chance(4) == 0 ? NO_MESH : 1u + chance(3));
                break;
            case 12:
                lastOp = "setLayer/tag";
                world.setLayer(e, 1u << chance(4));
                world.setTag(e, tags[chance(4)]);
                world.setName(e, "n" + std::to_string(chance(50)));
                break;
            case 13:
                lastOp = "add Health";
                world.add<Health>(e, static_cast<int>(chance(100)));
                break;
            case 14:
                lastOp = "add Link";
                world.add<Link>(e, Link{other});
                break;
            case 15:
                lastOp = "remove component";
                if (chance(2) == 0) {
                    world.remove<Health>(e);
                } else {
                    world.remove<Link>(e);
                }
                break;
            case 16:
                lastOp = "script";
                if (chance(3) == 0) {
                    world.removeScripts(e);
                } else {
                    world.attach<Patrol>(e, static_cast<int>(chance(9)), 1.0f);
                }
                break;
            case 17: {
                lastOp = "destroyBatch";
                std::vector<EntityHandle> doomed;
                for (EntityHandle a : alive) {
                    if (chance(12) == 0) doomed.push_back(a);
                }
                world.destroyBatch(doomed);
                break;
            }
            case 18:
                lastOp = "compact";
                (void)world.compact();
                break;
            case 19:
                lastOp = "raycast";
                (void)world.raycast(randomVec(), randomVec());
                break;
            case 20:
                lastOp = "overlapSphere";
                (void)world.overlapSphere(randomVec(), 20.0f + float(chance(80)));
                break;
            case 21:
                lastOp = "queryFrustum";
                world.camera().setPos(randomVec());
                world.camera().setOrientation(float(chance(360)), float(chance(120)) - 60.0f);
                (void)world.queryFrustum(world.camera());
                break;
            case 22:
                lastOp = "getWorldMatrix";
                (void)world.getWorldMatrix(e);
                (void)world.getWorldPosition(other);
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
                    const std::string text = world.snapshot();
                    if (!world.restore(text)) failed = true;
                    history.clear();
                }
                break;
            case 29:
                lastOp = "animation";
                if (chance(4) == 0) {
                    std::vector<glm::vec3> base;
                    for (uint32_t i = 0; i < 1 + chance(6); ++i) base.push_back(randomVec());
                    world.setAnimation(chance(8), base);
                }
                world.setAnimationTime(float(chance(1000)) / 37.0f);
                break;
            case 30:
                lastOp = "update";
                world.update(0.016f);
                break;
            case 31:
                lastOp = "setPositions batch";
                {
                    std::vector<std::pair<EntityHandle, glm::vec3>> moves;
                    for (EntityHandle a : alive) {
                        if (chance(5) == 0) moves.emplace_back(a, randomVec());
                    }
                    world.setPositions(moves);
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
                    const Prefab prefab = world.capture(e);
                    world.instantiate(prefab, chance(2) == 0 ? other : NULL_ENTITY);
                }
                break;
        }
        const std::vector<std::string> problems = world.validate();
        if (!problems.empty()) {
            std::fprintf(stderr, "soak op %d (%s) broke invariants: %s\n", op, lastOp.c_str(), problems[0].c_str());
            failed = true;
        }
    }
    CHECK(!failed);
    CHECK(world.validate().empty());
}

}

void runRobustnessTests() {
    testSceneVersioningAndTruncation();
    testLoaderHardeningAndFuzz();
    testSoakInvariants();
}
