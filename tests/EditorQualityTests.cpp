#include "TestSupport.hpp"
#include "Engine/Render/QualityPresets.hpp"

#include <sstream>
#include <string>
#include <string_view>

import Engine.Render.settings;

namespace {

bool applyByName(RenderSettings& settings, std::string_view name) {
    return quality::applyPresetWith(name, [&](std::string_view key, std::string_view value) { return setRenderSetting(settings, key, value); });
}

std::string allValues(const RenderSettings& settings) {
    std::ostringstream out;
    writeRenderSettings(settings, out, "");
    return out.str();
}

void testEveryPresetKeyIsAccepted() {
    for (const quality::Preset& preset : quality::kPresets) {
        RenderSettings settings;
        CHECK(applyByName(settings, preset.name));
        for (const quality::Setting& setting : preset.settings) CHECK(!renderSettingValue(settings, setting.key).empty());
    }
}

void testPresetsAreOrdered() {
    RenderSettings low;
    RenderSettings ultra;
    CHECK(applyByName(low, "low"));
    CHECK(applyByName(ultra, "ultra"));
    CHECK(low.renderScale < ultra.renderScale);
    CHECK(low.reflectionMode == ReflectionMode::SSR);
    CHECK(ultra.reflectionMode == ReflectionMode::WorldSpace);
    CHECK(!low.colorBleed && ultra.colorBleed);
    CHECK(!low.areaLights && ultra.areaLights);
    CHECK(low.shadowResolution < ultra.shadowResolution);
    CHECK(low.qualityGovernor && !ultra.qualityGovernor);
}

void testDefaultPresetRestoresEngineDefaults() {
    for (const quality::Preset& preset : quality::kPresets) {
        RenderSettings settings;
        CHECK(applyByName(settings, preset.name));
        CHECK(applyByName(settings, "default"));
        CHECK(allValues(settings) == allValues(RenderSettings{}));
    }
}

void testPerformancePresetValues() {
    RenderSettings balanced;
    CHECK(setRenderSetting(balanced, "preset", "balanced"));
    CHECK(balanced.renderScale == quantizeRenderScale(0.85f));
    CHECK(balanced.ssgiStepCount == 8);
    CHECK(balanced.aoSlices == 2 && balanced.aoSteps == 6);
    CHECK(balanced.clusterShadowErrorTexels == 4.0f);
    CHECK(!balanced.contactShadows && balanced.pcss);
    CHECK(!balanced.qualityGovernor);

    RenderSettings performance;
    CHECK(setRenderSetting(performance, "preset", " performance"));
    CHECK(performance.renderScale == quantizeRenderScale(0.75f));
    CHECK(performance.ssgiStepCount == 6);
    CHECK(performance.aoSlices == 2 && performance.aoSteps == 6);
    CHECK(performance.clusterShadowErrorTexels == 4.0f);
    CHECK(!performance.contactShadows && performance.pcss);
    CHECK(!performance.qualityGovernor);
    CHECK(performance.colorBleed && performance.bloom && performance.fxaa);
    CHECK(!setRenderSetting(performance, "preset", "nonsense"));
    CHECK(performance.ssgiStepCount == 6);
}

void testUnknownPresetChangesNothing() {
    RenderSettings settings;
    const float scale = settings.renderScale;
    CHECK(!applyByName(settings, "nonsense"));
    CHECK(settings.renderScale == scale);
    CHECK(quality::findPreset("high") != nullptr);
    CHECK(quality::findPreset("") == nullptr);
}

}

int main() {
    testEveryPresetKeyIsAccepted();
    testPresetsAreOrdered();
    testDefaultPresetRestoresEngineDefaults();
    testPerformancePresetValues();
    testUnknownPresetChangesNothing();
    return testsupport::finish();
}
