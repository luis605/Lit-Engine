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

static void testPostFxAndDataReload() {
    const std::string paramsPath = tempPath("fx_params.json");
    const std::string fxPath = tempPath("fx_post.json");
    std::ofstream(paramsPath) << R"({"params": {"focus": {"default": 1}}, "mappings": {"width": {"input": "focus", "curve": [[0, 0.3], [1, 0.9]]}}})";
    std::ofstream(fxPath) << R"({"vignetteWidth": {"base": 0.5, "param": "width"}, "maskContrast": 1.5, "maskSheenColor": [0.1, 0.2, 0.3]})";
    Engine engine;
    engine.openHeadless();
    std::string err;
    CHECK(engine.params().loadFile(paramsPath, &err));
    CHECK(engine.loadPostFx(fxPath, &err));
    CHECK(std::fabs(engine.renderSettings().maskContrast - 1.5f) < 1e-5f);
    CHECK(std::fabs(engine.renderSettings().maskSheenColor.z - 0.3f) < 1e-5f);
    engine.params().snap("focus", 0.0f);
    engine.runHeadless(4, 1.0f / 60.0f);
    CHECK(std::fabs(engine.renderSettings().vignetteWidth - 0.3f) < 1e-3f);
    engine.params().snap("focus", 1.0f);
    engine.runHeadless(4, 1.0f / 60.0f);
    CHECK(std::fabs(engine.renderSettings().vignetteWidth - 0.9f) < 1e-3f);
    std::ofstream(fxPath) << R"({"maskContrast": 2.25})";
    std::filesystem::last_write_time(fxPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
    engine.runHeadless(32, 1.0f / 60.0f);
    CHECK(engine.dataReloads() >= 1);
    CHECK(std::fabs(engine.renderSettings().maskContrast - 2.25f) < 1e-5f);
    std::ofstream(fxPath) << R"({"nonsense": 1})";
    std::filesystem::last_write_time(fxPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10));
    engine.runHeadless(32, 1.0f / 60.0f);
    CHECK(std::fabs(engine.renderSettings().maskContrast - 2.25f) < 1e-5f);
    CHECK(!engine.loadPostFx(fxPath, &err));
    std::filesystem::remove(paramsPath);
    std::filesystem::remove(fxPath);
}

static void testRenderMaskFlag() {
    Engine engine;
    engine.openHeadless();
    EntityDesc desc;
    desc.name = "masked";
    desc.mesh = 0;
    const EntityHandle e = engine.world().create(desc);
    CHECK((engine.world().getRenderFlags(e) & RENDER_MASKED) == 0);
    engine.world().setRenderFlags(e, RENDER_MASKED, true);
    CHECK((engine.world().getRenderFlags(e) & RENDER_MASKED) != 0);
    engine.world().setRenderFlags(e, RENDER_MASKED, false);
    CHECK((engine.world().getRenderFlags(e) & RENDER_MASKED) == 0);
}

static void testColorBleedSettings() {
    RenderSettings settings;
    CHECK(settings.colorBleed);
    CHECK(setRenderSetting(settings, "colorBleed", "0"));
    CHECK(!settings.colorBleed);
    CHECK(setRenderSetting(settings, "colorBleed", "1"));
    CHECK(setRenderSetting(settings, "colorBleedIntensity", "1.75"));
    CHECK(setRenderSetting(settings, "colorBleedRadius", "4"));
    CHECK(setRenderSetting(settings, "colorBleedQuality", "2"));
    CHECK(renderSettingValue(settings, "colorBleedQuality") == "2");
    CHECK(renderSettingValue(settings, "colorBleedIntensity") == "1.75");
    std::ostringstream out;
    writeRenderSettings(settings, out, "render.");
    CHECK(out.str().find("render.colorBleedRadius 4") != std::string::npos);
    RenderSettings restored;
    std::istringstream lines(out.str());
    std::string line;
    while (std::getline(lines, line)) {
        const size_t split = line.find(' ');
        setRenderSetting(restored, std::string_view(line).substr(7, split - 7), std::string_view(line).substr(split + 1));
    }
    CHECK(restored == settings);
    CHECK(colorBleedTapCount(0) < colorBleedTapCount(3));
    CHECK(colorBleedTapCount(-5) == colorBleedTapCount(0));
    CHECK(colorBleedTapCount(99) == colorBleedTapCount(kColorBleedQualityMax));
    glm::vec4 uniform = colorBleedUniform(settings);
    CHECK(uniform.x == 1.75f && uniform.y == 4.0f && uniform.z == static_cast<float>(colorBleedTapCount(2)));
    settings.colorBleedIntensity = 100.0f;
    settings.colorBleedRadius = 1000.0f;
    uniform = colorBleedUniform(settings);
    CHECK(uniform.x == kColorBleedIntensityMax && uniform.y == kColorBleedRadiusMax);
    settings.ssao = false;
    CHECK(!colorBleedActive(settings));
    CHECK(colorBleedUniform(settings) == glm::vec4(0.0f));
    settings.ssao = true;
    settings.colorBleed = false;
    CHECK(colorBleedUniform(settings).x == 0.0f);
}

// Small knobs so the stress scenes stay cheap on the CPU-only path.
static TestSceneParams smallParams() {
    TestSceneParams params;
    params.lightCount = 64;
    params.denseGrid = 3;
    params.denseDetail = 2;
    params.largeWorld.flySpeed = 12.0f;
    params.largeWorld.rebaseFrame = 30;
    return params;
}

// Switching scenes on one Engine repeatedly must leave nothing behind: no testscene.* systems, no floating origin, origin back at zero.
static void testSceneSwitching() {
    Engine engine;
    engine.openHeadless();
    engine.seed(7);
    const std::vector<std::string> names = testSceneNames();
    for (int round = 0; round < 2; ++round) {
        for (const std::string& name : names) {
            engine.world().clear();
            engine.portals().clear();
            TestSceneInfo info;
            CHECK(buildTestScene(engine, name, TestSceneKit{}, info, smallParams()));
            applyTestSceneCamera(engine, info);
            engine.runHeadless(40, 1.0f / 60.0f);
            CHECK(engine.world().aliveCount() > 5);
            if (name != "largeworld") CHECK(engine.worldOrigin() == glm::dvec3(0.0));
        }
    }
    engine.world().clear();
    leaveTestScene(engine);
    CHECK(engine.worldOrigin() == glm::dvec3(0.0));
    for (const Phase phase : {Phase::PreUpdate, Phase::FixedUpdate, Phase::Update, Phase::PostUpdate}) {
        for (const std::string& system : engine.systemNames(phase)) {
            CHECK(!system.starts_with("testscene."));
            CHECK(system != "floating_origin");
        }
    }
}

static void testTestScenes() {
    const std::vector<std::string> names = testSceneNames();
    CHECK(names.size() == 15);
    for (const char* expected : {"showcase", "probes", "light_stress", "largeworld", "vsm_mixed", "dense", "ik", "gallery"}) CHECK(std::ranges::find(names, std::string(expected)) != names.end());
    for (const std::string& name : testSceneNames()) {
        Engine engine;
        engine.openHeadless();
        engine.seed(7);
        TestSceneInfo info;
        CHECK(buildTestScene(engine, name, TestSceneKit{}, info, smallParams()));
        applyTestSceneCamera(engine, info);
        engine.runHeadless(name == "streaming_route" ? 900 : 120, 1.0f / 60.0f);
        const TestSceneMetrics m = info.metrics();
        CHECK(engine.world().aliveCount() > 5);
        if (name == "portal_loop") {
            CHECK(m.get("cells") == 3.0);
            CHECK(m.get("portals") == 6.0);
        } else if (name == "portal_torture") {
            CHECK(m.get("portals") >= 4.0);
            CHECK(m.get("max_view_nodes") >= 2.0);
        } else if (name == "streaming_route") {
            CHECK(m.get("chunks_created") > 9.0);
            CHECK(m.get("chunks_destroyed") > 0.0);
            CHECK(m.get("chunks_resident") == 9.0);
        } else if (name == "crowd") {
            CHECK(m.get("agents") >= 500.0);
            CHECK(m.get("distance_walked_m") > 100.0);
        } else if (name == "skinning") {
            CHECK(m.get("dynamic_instances") >= 1.0);
            CHECK(m.get("joints") > 1.0);
        } else if (name == "terrain") {
            CHECK(m.get("terrain_resident") > 0.0);
            CHECK(m.get("travelled_m") > 0.0);
        } else if (name == "light_stress") {
            CHECK(m.get("moving_lights") == 64.0);
            CHECK(m.get("moving_casters") == 256.0);
        } else if (name == "vsm_mixed") {
            CHECK(m.get("characters") == 4.0);
            CHECK(m.get("moving_lights") == 64.0);
            CHECK(m.get("cells") == 3.0);
        } else if (name == "dense") {
            CHECK(m.get("rocks") == 9.0);
        } else if (name == "largeworld") {
            CHECK(m.get("camera_world_x_km") > 499.0);
            CHECK(m.get("camera_local_m") < 400.0);
        } else if (name == "vista") {
            CHECK(m.get("farthest_object_m") > 50000.0);
            CHECK(m.get("depth_step_m_at_10000") > 0.0);
        }
        Engine again;
        again.openHeadless();
        again.seed(7);
        TestSceneInfo second;
        buildTestScene(again, name, TestSceneKit{}, second, smallParams());
        applyTestSceneCamera(again, second);
        again.runHeadless(name == "streaming_route" ? 900 : 120, 1.0f / 60.0f);
        CHECK(again.dumpStateText() == engine.dumpStateText());
    }
    TestSceneInfo none;
    Engine engine;
    engine.openHeadless();
    CHECK(!buildTestScene(engine, "missing", TestSceneKit{}, none));
    CHECK(forwardDepthStep(0.1f, 100000.0f, 10000.0f) > forwardDepthStep(0.5f, 100000.0f, 10000.0f));
}

void runScenesTests() {
    testPostFxAndDataReload();
    testRenderMaskFlag();
    testColorBleedSettings();
    testTestScenes();
    testSceneSwitching();
}
