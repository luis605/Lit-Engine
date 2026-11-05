#include "TestSupport.hpp"
#include "Editor/Look/QualityPresets.hpp"

#include <string>
#include <string_view>

import Engine.Render.settings;

namespace {

bool applyByName(RenderSettings& settings, std::string_view name) {
    return quality::applyPresetWith(name, [&](std::string_view key, std::string_view value) { return setRenderSetting(settings, key, value); });
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
    testUnknownPresetChangesNothing();
    return testsupport::finish();
}
