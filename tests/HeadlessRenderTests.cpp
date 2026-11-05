#include "TestSupport.hpp"
#include <cmath>
#include <string>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.glm;
import Engine.Render.settings;
import Engine.Render.reflection;

namespace {

constexpr int kTicksPerCase = 6;
constexpr float kTickSeconds = 1.0f / 60.0f;
const std::vector<std::string> kExtremeValues = {"0", "1", "-1", "2", "3", "7", "1000", "-1000", "1e9", "0.001"};

void addReflectionVolumes(World& world) {
    const auto probeEntity = world.create("probe", 0, glm::vec3(1.0f, 2.0f, 3.0f));
    ReflectionProbeComponent probeData;
    probeData.halfExtents = glm::vec3(4.0f);
    world.add<ReflectionProbeComponent>(probeEntity, probeData);
    const auto planeEntity = world.create("mirror", 0, glm::vec3(0.0f, 0.5f, 0.0f));
    world.add<PlanarReflectionComponent>(planeEntity, PlanarReflectionComponent{});
}

void tickEngine(Engine& engine) {
    for (int tickIndex = 0; tickIndex < kTicksPerCase; ++tickIndex) engine.tick(kTickSeconds);
}

std::vector<std::string> settingNames() {
    std::vector<std::string> names;
    RenderSettings settings;
    visitRenderSettings(settings, [&](std::string_view name, auto&) { names.emplace_back(name); });
    return names;
}

void testEveryReflectionModeAndTier() {
    Engine engine;
    engine.openHeadless();
    addReflectionVolumes(engine.world());
    for (int mode = 0; mode < kReflectionModeCount; ++mode) {
        for (int quality = 0; quality < kReflectionQualityCount; ++quality) {
            CHECK(setRenderSetting(engine.renderSettings(), "reflectionMode", std::to_string(mode)));
            CHECK(setRenderSetting(engine.renderSettings(), "reflectionQuality", std::to_string(quality)));
            tickEngine(engine);
        }
    }
}

void testAllNewFeaturesEnabled() {
    Engine engine;
    engine.openHeadless();
    addReflectionVolumes(engine.world());
    RenderSettings& settings = engine.renderSettings();
    settings.reflectionMode = ReflectionMode::WorldSpace;
    settings.reflectionQuality = ReflectionQuality::High;
    settings.colorBleed = true;
    settings.colorBleedQuality = 2;
    settings.contactShadows = true;
    settings.bentNormals = true;
    settings.areaLights = true;
    settings.qualityGovernor = true;
    settings.renderScale = 0.5f;
    settings.upscaleSharpness = 1.0f;
    tickEngine(engine);
    CHECK(settings.renderScale >= 0.5f && settings.renderScale <= 1.0f);
}

void testExtremeValuesForEverySetting() {
    Engine engine;
    engine.openHeadless();
    addReflectionVolumes(engine.world());
    const RenderSettings defaults;
    for (const std::string& name : settingNames()) {
        for (const std::string& value : kExtremeValues) {
            setRenderSetting(engine.renderSettings(), name, value);
            tickEngine(engine);
        }
        engine.renderSettings() = defaults;
    }
    CHECK(true);
}

void testSettingsStayWithinClampedRanges() {
    RenderSettings settings;
    for (const std::string& name : settingNames()) {
        for (const std::string& value : kExtremeValues) setRenderSetting(settings, name, value);
    }
    CHECK(settings.renderScale >= kRenderScaleMin && settings.renderScale <= kRenderScaleMax);
    CHECK(settings.aoDenoise >= 0 && settings.aoDenoise <= kAoDenoiseMax);
    CHECK(std::isfinite(settings.reflectionTraceDistance));
}

}

int main() {
    testEveryReflectionModeAndTier();
    testAllNewFeaturesEnabled();
    testExtremeValuesForEverySetting();
    testSettingsStayWithinClampedRanges();
    return testsupport::finish();
}
