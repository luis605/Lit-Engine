#include "TestSupport.hpp"
#include "ToolingSuites.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;
import Engine.Rng;
import Engine.Json;
import Engine.Dump;
import Engine.Replay;
import Engine.Perf;
import Engine.Params;
import Engine.Render.settings;
import Engine.TestScenes;

static std::string tempPath(const std::string& name) { return testsupport::tempFilePath("lit_tooling_", name); }

static std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void testStateDump() {
    Engine engine;
    engine.openHeadless();
    World& w = engine.world();
    auto parent = w.create("parent", NO_MESH, glm::vec3(1.0f, 2.0f, 3.0f));
    auto child = w.create("child", NO_MESH, glm::vec3(0.5f, 0.0f, 0.0f));
    w.setParent(child, parent, false);
    w.setTag(child, "tagged");
    w.setLayer(child, 4);
    const Json doc = dumpWorld(w);
    CHECK_OK(expectNumber(doc, "count", 2));
    CHECK_OK(expectVec(doc, "entities[name=parent].position", {1.0, 2.0, 3.0}));
    CHECK_OK(expectVec(doc, "entities[name=child].worldPosition", {1.5, 2.0, 3.0}));
    CHECK_OK(expectNumber(doc, "entities[name=child].parent", parent.index));
    CHECK_OK(expectNumber(doc, "entities[name=parent].parent", -1));
    CHECK_OK(expectString(doc, "entities[name=child].tag", "tagged"));
    CHECK_OK(expectNumber(doc, "entities[name=child].layer", 4));
    CHECK_OK(expectNumber(doc, "entities[name=parent].children", 1));
    CHECK_OK(expectBool(doc, "entities[name=child].visible", true));
    const std::string path = tempPath("state.json");
    engine.params().set("drive", 0.5f);
    engine.params().update(0.016f);
    CHECK(engine.writeStateDump(path));
    const auto file = Json::parse(slurp(path));
    CHECK(file.has_value());
    CHECK_OK(expectNumber(*file, "params.drive", 0.5));
    CHECK_OK(expectNumber(*file, "world.count", 2));
    std::filesystem::remove(path);

    World other;
    other.create("parent", NO_MESH, glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(!jsonDiff(doc, dumpWorld(other)).empty());
    DumpOptions lean;
    lean.components = false;
    lean.scripts = false;
    lean.worldTransforms = false;
    CHECK(!jsonFind(dumpWorld(w, lean), "entities[0].components"));
}

static std::vector<std::vector<std::string>> readCsv(const std::string& path) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) cells.push_back(cell);
        rows.push_back(std::move(cells));
    }
    return rows;
}

static void testPerfLog() {
    const std::string path = tempPath("perf.csv");
    {
        PerfLog log;
        CHECK(!log.active());
        log.open(path);
        CHECK(log.active());
        log.record(0, 16.0, 1.0, 4.0, {{"Shadow", 1.5}, {"GBuffer", 2.5}});
        log.record(1, 17.0, 1.1, 4.5, {{"GBuffer", 3.0}, {"Post,Fx", 0.5}});
        CHECK(log.rows() == 2);
    }
    const auto rows = readCsv(path);
    CHECK(rows.size() == 3);
    CHECK(rows[0].size() == 7);
    CHECK(rows[0][0] == "frame" && rows[0][1] == "cpu_ms" && rows[0][3] == "gpu_ms");
    CHECK(rows[0][4] == "gpu:Shadow" && rows[0][5] == "gpu:GBuffer" && rows[0][6] == "gpu:Post_Fx");
    CHECK(rows[1].size() == 7 && std::abs(std::stod(rows[1][5]) - 2.5) < 1.0e-6);
    CHECK(std::abs(std::stod(rows[2][5]) - 3.0) < 1.0e-6 && std::stod(rows[2][4]) == 0.0);
    std::filesystem::remove(path);

    Engine engine;
    engine.openHeadless();
    engine.enablePerfLog(path);
    CHECK(engine.perfLog().active());
    engine.runHeadless(30, 1.0f / 60.0f);
    engine.disablePerfLog();
    const auto engineRows = readCsv(path);
    CHECK(engineRows.size() == 31);
    CHECK(engineRows[0].size() == 4);
    CHECK(std::stod(engineRows[10][1]) >= 0.0);
    CHECK(std::stoull(engineRows[30][0]) == 29);
    std::filesystem::remove(path);
}

static void testParams() {
    ParamBus bus;
    bus.declare("drive", 0.0f, 0.5f, 0.0f, 1.0f);
    bus.set("drive", 1.0f);
    CHECK(bus.get("drive") == 0.0f);
    bus.update(0.5f);
    CHECK(std::abs(bus.get("drive") - (1.0f - std::exp(-1.0f))) < 1.0e-5f);
    for (int i = 0; i < 200; ++i) bus.update(0.1f);
    CHECK(bus.get("drive") > 0.999f);
    bus.set("drive", 5.0f);
    CHECK(bus.target("drive") == 1.0f);
    CHECK(bus.slot("drive") == 0);
    CHECK(bus.slot("missing") == -1);
    bus.snap("drive", 0.25f);
    CHECK(bus.get("drive") == 0.25f);
    CHECK(bus.values()[0] == 0.25f);
    bus.set("auto", 2.0f);
    bus.update(0.016f);
    CHECK(bus.get("auto") == 2.0f && bus.slot("auto") == 1);

    ParamCurve curve;
    curve.points = {{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}};
    CHECK(curve.eval(-1.0f) == 0.0f);
    CHECK(std::abs(curve.eval(0.25f) - 0.5f) < 1.0e-6f);
    CHECK(curve.eval(2.0f) == 0.0f);
    curve.mode = CurveMode::Step;
    CHECK(curve.eval(0.4f) == 0.0f && curve.eval(0.6f) == 1.0f);
    curve.mode = CurveMode::Smooth;
    CHECK(std::abs(curve.eval(0.25f) - 0.5f) < 1.0e-6f);
    CHECK(curve.eval(0.1f) < 0.1f * 2.0f);

    const std::string path = tempPath("params.json");
    const char* v1 = R"({
        "params": {"drive": {"default": 0, "min": 0, "max": 1, "smooth": 0, "slot": 4}, "bias": {"default": 0.5}},
        "mappings": {
            "vignette": {"input": "drive", "curve": [[0, 0.1], [1, 0.9]], "slot": 9},
            "grade": {"input": "vignette", "curve": [[0, 0], [1, 10]], "mode": "step"}
        }
    })";
    {
        std::ofstream(path) << v1;
    }
    ParamBus file;
    std::string err;
    CHECK(file.loadFile(path, &err));
    CHECK(file.slot("drive") == 4 && file.slot("vignette") == 9 && file.slot("bias") == 0);
    file.set("drive", 0.5f);
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.5f) < 1.0e-6f);
    CHECK(file.get("grade") == 0.0f);
    file.set("drive", 1.0f);
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.9f) < 1.0e-6f);
    CHECK(file.get("grade") == 0.0f);
    CHECK(file.valueAt(9) == file.get("vignette"));
    CHECK(file.curve("vignette") != nullptr && file.curve("drive") == nullptr);
    CHECK(!file.pollReload());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream(path) << R"({"params": {"drive": {"default": 0, "min": 0, "max": 1, "slot": 4}, "bias": {"default": 0.5}},
            "mappings": {"vignette": {"input": "drive", "curve": [[0, 0.0], [1, 0.2]], "slot": 9}}})";
    }
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));
    const uint32_t before = file.reloadCount();
    CHECK(file.pollReload(&err));
    CHECK(file.reloadCount() == before + 1);
    CHECK(file.get("drive") == 1.0f);
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.2f) < 1.0e-6f);
    CHECK(!file.has("grade"));
    CHECK(!file.pollReload());

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream(path) << R"({"mappings": {"broken": {"input": "nothing", "curve": [[0, 0]]}}})";
    }
    std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(4));
    CHECK(!file.pollReload(&err));
    CHECK(!err.empty());
    CHECK(!file.has("broken"));
    file.update(0.016f);
    CHECK(std::abs(file.get("vignette") - 0.2f) < 1.0e-6f);
    CHECK(!file.pollReload());
    std::filesystem::remove(path);

    ParamBus bad;
    CHECK(!bad.loadText("{", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"min": 2, "max": 1}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"wat": 1}}})", &err) && err.find("unknown key") != std::string::npos);
    CHECK(!bad.loadText(R"({"params": {"a": {}}, "mappings": {"m": {"input": "a", "curve": [[1, 0], [0, 1]]}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {}}, "mappings": {"m": {"input": "a", "curve": []}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"smooth": -1}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {"slot": 9999}}})", &err));
    CHECK(!bad.loadText(R"({"params": {"a": {}}, "mappings": {"m": {"input": "a", "curve": [[0, 0]], "mode": "wavy"}}})", &err));
    CHECK(bad.params().empty());
}

static void testEngineParamsSystem() {
    const std::string path = tempPath("engine_params.json");
    {
        std::ofstream(path) << R"({"params": {"drive": {"default": 0, "smooth": 0.1}}, "mappings": {"tight": {"input": "drive", "curve": [[0, 0], [1, 2]]}}})";
    }
    Engine engine;
    engine.openHeadless();
    std::string err;
    CHECK(engine.params().loadFile(path, &err));
    engine.params().set("drive", 1.0f);
    engine.runHeadless(120, 1.0f / 60.0f);
    CHECK(engine.params().get("drive") > 0.99f);
    CHECK(engine.params().get("tight") > 1.98f);
    const std::string a = engine.dumpStateText();
    Engine again;
    again.openHeadless();
    CHECK(again.params().loadFile(path, &err));
    again.params().set("drive", 1.0f);
    again.runHeadless(120, 1.0f / 60.0f);
    CHECK(again.dumpStateText() == a);
    std::filesystem::remove(path);
}

void runDumpParamsTests() {
    testStateDump();
    testPerfLog();
    testParams();
    testEngineParamsSystem();
}
