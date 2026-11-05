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

void testProfiler() {
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
    CHECK(profiler.topScopes(10).size() == 3);
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

void testLineEditor() {
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

void testGizmoMath() {
    Ray ray{glm::vec3(3.0f, 4.0f, 10.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
    CHECK(std::abs(gizmo::closestParameterOnAxis(ray, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)) - 3.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::closestParameterOnAxis(ray, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)) - 4.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::closestParameterOnAxis(ray, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)) - 2.0f) < 1.0e-5f);
    CHECK(gizmo::closestParameterOnAxis(ray, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)) == 0.0f);

    const auto hit = gizmo::rayPlane(ray, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(hit && nearlyEqual(*hit, glm::vec3(3.0f, 4.0f, 0.0f)));
    CHECK(!gizmo::rayPlane(ray, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    CHECK(!gizmo::rayPlane(Ray{glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, -1.0f)}, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

    const glm::vec3 y(0.0f, 1.0f, 0.0f);
    CHECK(std::abs(gizmo::signedAngleAroundAxis(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), y) - glm::radians(90.0f)) < 1.0e-4f);
    CHECK(std::abs(gizmo::signedAngleAroundAxis(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), y) + glm::radians(90.0f)) < 1.0e-4f);
    CHECK(std::abs(gizmo::signedAngleAroundAxis(glm::vec3(1.0f, 5.0f, 0.0f), glm::vec3(-2.0f, -3.0f, 0.0f), y) - glm::radians(180.0f)) < 1.0e-3f);

    CHECK(gizmo::snap(0.74f, 0.5f) == 0.5f && gizmo::snap(0.76f, 0.5f) == 1.0f && gizmo::snap(-0.3f, 0.5f) == -0.5f && gizmo::snap(1.234f, 0.0f) == 1.234f);

    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    const glm::mat4 rotated = gizmo::rotateAboutWorldAxis(world, glm::vec3(5.0f, 0.0f, 0.0f), y, glm::radians(90.0f));
    CHECK(nearlyEqual(glm::vec3(rotated[3]), glm::vec3(5.0f, 0.0f, 0.0f)));
    CHECK(nearlyEqual(glm::vec3(rotated[0]), glm::vec3(0.0f, 0.0f, -1.0f)));
    const glm::mat4 orbit = gizmo::rotateAboutWorldAxis(world, glm::vec3(0.0f), y, glm::radians(90.0f));
    CHECK(nearlyEqual(glm::vec3(orbit[3]), glm::vec3(0.0f, 0.0f, -5.0f)));

    glm::mat4 local = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) * glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), y)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    const glm::mat4 scaledX = gizmo::scaleAlongLocalAxis(local, 0, 3.0f, false);
    CHECK(std::abs(glm::length(glm::vec3(scaledX[0])) - 6.0f) < 1.0e-4f && std::abs(glm::length(glm::vec3(scaledX[1])) - 2.0f) < 1.0e-4f);
    CHECK(nearlyEqual(glm::vec3(scaledX[3]), glm::vec3(1.0f, 2.0f, 3.0f)));
    const glm::mat4 uniform = gizmo::scaleAlongLocalAxis(local, 1, 0.5f, true);
    CHECK(std::abs(glm::length(glm::vec3(uniform[2])) - 1.0f) < 1.0e-4f && std::abs(glm::length(glm::vec3(uniform[0])) - 1.0f) < 1.0e-4f);
    CHECK(std::abs(gizmo::distanceToSegment2D(glm::vec2(5.0f, 3.0f), glm::vec2(0.0f), glm::vec2(10.0f, 0.0f)) - 3.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::distanceToSegment2D(glm::vec2(-4.0f, 3.0f), glm::vec2(0.0f), glm::vec2(10.0f, 0.0f)) - 5.0f) < 1.0e-5f);
    CHECK(std::abs(gizmo::distanceToSegment2D(glm::vec2(2.0f, 2.0f), glm::vec2(1.0f), glm::vec2(1.0f)) - std::sqrt(2.0f)) < 1.0e-5f);
}

void testSelectionAndGroups() {
    World world;
    auto a = world.create("a", 1);
    auto b = world.create("b", 1, glm::vec3(0.0f), glm::vec3(1.0f), a);
    auto c = world.create("c", 1, glm::vec3(0.0f), glm::vec3(1.0f), b);
    auto d = world.create("d", 1);

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
    sel.removeDescendantsOfSelected(world);
    CHECK(sel.size() == 2 && sel.contains(a) && sel.contains(d) && !sel.contains(b) && !sel.contains(c));
    world.destroy(d);
    sel.prune(world);
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

}

void runEditorToolsTests() {
    testProfiler();
    testLineEditor();
    testGizmoMath();
    testSelectionAndGroups();
}
