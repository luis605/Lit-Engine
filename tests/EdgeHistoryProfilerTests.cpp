#include "TestSupport.hpp"
#include "EdgeSuites.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

import Engine.History;
import Engine.Profiler;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

namespace {

void testUndoRedoBoundaries() {
    World world;
    History history(world);
    CHECK(!history.undo() && !history.redo());
    CHECK(!history.canUndo() && !history.canRedo());

    int value = 0;
    history.record("set 1", [&] { value = 1; }, [&] { value = 0; });
    history.record("set 2", [&] { value = 2; }, [&] { value = 1; });
    value = 2;
    CHECK(history.undoCount() == 2 && history.undoLabel(1) == "set 2");
    CHECK(history.undo() && value == 1);
    CHECK(history.canRedo() && history.redoCount() == 1 && history.redoLabel(0) == "set 2");
    history.record("set 9", [&] { value = 9; }, [&] { value = 1; });
    CHECK(!history.canRedo());
    CHECK(history.undo() && history.undo() && value == 0);
    CHECK(!history.undo());
    CHECK(history.redo() && history.redo() && value == 9);
    CHECK(!history.redo());
}

void testLimitAndClear() {
    World world;
    History history(world, 3);
    int value = 0;
    for (int step = 1; step <= 5; ++step) history.record("step", [&value, step] { value = step; }, [&value, step] { value = step - 1; });
    CHECK(history.undoCount() == 3);
    const uint64_t before = history.state();
    history.clear();
    CHECK(!history.canUndo() && history.state() != before);

    History zero(world, 0);
    zero.record("x", [] {}, [] {});
    CHECK(zero.undoCount() == 0 && !zero.undo());
}

void testGroupEdges() {
    World world;
    History history(world);
    history.endGroup();
    CHECK(!history.inGroup());
    history.beginGroup("empty");
    history.endGroup();
    CHECK(!history.canUndo());

    int value = 0;
    history.beginGroup("outer");
    history.beginGroup("inner ignored");
    history.record("a", [&] { value += 1; }, [&] { value -= 1; });
    history.record("b", [&] { value += 10; }, [&] { value -= 10; });
    value = 11;
    history.endGroup();
    CHECK(history.inGroup() && !history.canUndo());
    history.endGroup();
    CHECK(history.undoCount() == 1 && history.undoLabel(0) == "outer");
    CHECK(history.undo() && value == 0);
    CHECK(history.redo() && value == 11);

    history.beginGroup();
    history.record("only", [] {}, [] {});
    history.record("second", [] {}, [] {});
    history.endGroup();
    CHECK(history.undoLabel(1) == "only x2");

    history.beginGroup("discard");
    history.record("lost", [] {}, [] {});
    history.clear();
    CHECK(!history.inGroup() && !history.canUndo());
    history.endGroup();
    CHECK(!history.canUndo());
}

void testEntityLifecycleUndo() {
    World world;
    History history(world);
    const EntityHandle entity = world.create("subject", 1);
    history.setLocalMatrix(entity, glm::translate(glm::mat4(1.0f), glm::vec3(1, 2, 3)));
    history.destroy(entity);
    CHECK(!world.isAlive(entity));
    CHECK(history.undo());
    const EntityHandle revived = history.resolve(entity);
    CHECK(world.isAlive(revived));
    CHECK(glm::length(glm::vec3(world.getLocalMatrix(revived)[3]) - glm::vec3(1, 2, 3)) < 1.0e-5f);
    CHECK(history.undo());
    CHECK(glm::length(glm::vec3(world.getLocalMatrix(history.resolve(entity))[3])) < 1.0e-5f);
    CHECK(history.redo() && history.redo());
    CHECK(!world.isAlive(history.resolve(entity)));

    history.clear();
    history.setLocalMatrix(NULL_ENTITY, glm::mat4(1.0f));
    CHECK(!history.canUndo());
}

void testProfilerEdges() {
    Profiler profiler(0);
    CHECK(profiler.begin("off") == -1);
    profiler.end(-1);
    profiler.beginFrame();
    profiler.endFrame();
    CHECK(profiler.frameIndex() == 0 && profiler.currentFrame().empty());

    profiler.setEnabled(true);
    for (int frame = 0; frame < 4; ++frame) {
        profiler.beginFrame();
        {
            ProfileScope outer(profiler, "outer");
            ProfileScope inner(profiler, "inner");
        }
        { ProfileScope again(profiler, "inner"); }
    }
    profiler.endFrame();
    CHECK(profiler.history().size() == 1);
    CHECK(profiler.frameIndex() == 4);
    const auto top = profiler.topScopes(10);
    CHECK(top.size() == 2);
    for (const ProfileSummary& summary : top) {
        if (summary.name == "inner") CHECK(summary.calls == 2);
        if (summary.name == "outer") CHECK(summary.calls == 1);
    }
    CHECK(profiler.topScopes(0).empty());
    CHECK(profiler.topScopes(1).size() == 1);

    profiler.end(1000);
    profiler.end(5);

    profiler.beginFrame();
    const int64_t token = profiler.begin("quote\"back\\slash\nline");
    profiler.end(token);
    std::ostringstream trace;
    profiler.writeChromeTrace(trace);
    const std::string text = trace.str();
    CHECK(text.find("quote\\\"back\\\\slash line") != std::string::npos);
    CHECK(text.front() == '{' && text.back() == '}');

    CHECK(!profiler.writeChromeTrace(std::string("/nonexistent_dir_xyz/trace.json")));
    profiler.clear();
    CHECK(profiler.frameIndex() == 0 && profiler.history().empty() && profiler.topScopes(5).empty());
}

}

void runEdgeHistoryProfilerTests() {
    testUndoRedoBoundaries();
    testLimitAndClear();
    testGroupEdges();
    testEntityLifecycleUndo();
    testProfilerEdges();
}
