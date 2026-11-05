#include "TestSupport.hpp"
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.governor;

namespace {

constexpr float kStep = 1.0f / 60.0f;
constexpr float kTarget = 16.6f;

int runSeconds(QualityGovernor& governor, RenderSettings& settings, float frameMs, float seconds) {
    int changes = 0;
    const int frames = static_cast<int>(seconds / kStep);
    for (int frame = 0; frame < frames; ++frame) {
        const int before = governor.level;
        updateGovernor(governor, settings, frameMs, kStep);
        if (governor.level != before) ++changes;
    }
    return changes;
}

void testQualitySettings() {
    RenderSettings settings;
    CHECK(settings.renderScale == 1.0f);
    CHECK(setRenderSetting(settings, "renderScale", "0.7"));
    CHECK(std::abs(settings.renderScale - 0.7f) < 1e-4f);
    CHECK(setRenderSetting(settings, "renderScale", "0.1"));
    CHECK(settings.renderScale == kRenderScaleMin);
    CHECK(setRenderSetting(settings, "renderScale", "7"));
    CHECK(settings.renderScale == kRenderScaleMax);
    CHECK(setRenderSetting(settings, "renderScale", "0.73"));
    CHECK(std::abs(settings.renderScale - 0.75f) < 1e-4f);
    CHECK(setRenderSetting(settings, "upscaleSharpness", "5"));
    CHECK(settings.upscaleSharpness == 1.0f);
    CHECK(setRenderSetting(settings, "targetFrameMs", "0"));
    CHECK(settings.targetFrameMs == kTargetFrameMsMin);
    CHECK(setRenderSetting(settings, "aoDenoise", "9"));
    CHECK(settings.aoDenoise == kAoDenoiseMax);
    CHECK(setRenderSetting(settings, "shadowCascadeBlend", "-1"));
    CHECK(settings.shadowCascadeBlend == 0.0f);
    CHECK(setRenderSetting(settings, "qualityGovernor", "1"));
    CHECK(settings.qualityGovernor);
    CHECK(setRenderSetting(settings, "bentNormals", "0"));
    CHECK(setRenderSetting(settings, "areaLights", "0"));
    std::ostringstream out;
    writeRenderSettings(settings, out, "render.");
    RenderSettings restored;
    std::istringstream lines(out.str());
    std::string line;
    while (std::getline(lines, line)) {
        const size_t split = line.find(' ');
        setRenderSetting(restored, std::string_view(line).substr(7, split - 7), std::string_view(line).substr(split + 1));
    }
    CHECK(restored == settings);
    CHECK(quantizeRenderScale(0.52f) == 0.5f);
    CHECK(quantizeRenderScale(1.4f) == 1.0f);
}

void testGovernorStaysAtFullQualityOnTarget() {
    RenderSettings settings;
    settings.qualityGovernor = true;
    QualityGovernor governor;
    CHECK(runSeconds(governor, settings, kTarget * 0.9f, 20.0f) == 0);
    CHECK(governor.level == 0);
    CHECK(settings.renderScale == 1.0f);
}

void testGovernorStepsDownSlowly() {
    RenderSettings settings;
    settings.qualityGovernor = true;
    QualityGovernor governor;
    runSeconds(governor, settings, kTarget * 1.6f, 1.0f);
    CHECK(governor.level == 0);
    runSeconds(governor, settings, kTarget * 1.6f, 1.0f);
    CHECK(governor.level == 1);
    CHECK(settings.renderScale < 1.0f);
    const int changes = runSeconds(governor, settings, kTarget * 1.6f, 60.0f);
    CHECK(governor.level == kGovernorMaxLevel);
    CHECK(changes <= kGovernorMaxLevel);
    CHECK(settings.renderScale >= settings.governorMinScale - 1e-4f);
    CHECK(settings.shadowResolution < 2048);
    CHECK(static_cast<int>(settings.reflectionQuality) < 2);
}

void testGovernorRecoversWithHysteresis() {
    RenderSettings settings;
    settings.qualityGovernor = true;
    QualityGovernor governor;
    runSeconds(governor, settings, kTarget * 1.6f, 12.0f);
    const int lowered = governor.level;
    CHECK(lowered >= 2);
    runSeconds(governor, settings, kTarget * 0.9f, 20.0f);
    CHECK(governor.level == lowered);
    runSeconds(governor, settings, kTarget * 0.4f, 8.0f);
    CHECK(governor.level == lowered - 1);
    runSeconds(governor, settings, kTarget * 0.4f, 120.0f);
    CHECK(governor.level == 0);
    CHECK(settings.renderScale == 1.0f);
    CHECK(settings.shadowResolution == 2048);
}

void testGovernorDoesNotOscillate() {
    RenderSettings settings;
    settings.qualityGovernor = true;
    QualityGovernor governor;
    int changes = 0;
    for (int cycle = 0; cycle < 40; ++cycle) {
        changes += runSeconds(governor, settings, cycle % 2 == 0 ? kTarget * 1.4f : kTarget * 0.6f, 3.0f);
    }
    CHECK(changes <= 20);
    RenderSettings replay;
    replay.qualityGovernor = true;
    QualityGovernor second;
    int replayChanges = 0;
    for (int cycle = 0; cycle < 40; ++cycle) replayChanges += runSeconds(second, replay, cycle % 2 == 0 ? kTarget * 1.4f : kTarget * 0.6f, 3.0f);
    CHECK(replayChanges == changes);
    CHECK(second.level == governor.level);
    CHECK(replay == settings);
}

void testGovernorBaselineAndDisable() {
    RenderSettings settings;
    settings.qualityGovernor = true;
    settings.renderScale = 0.8f;
    settings.shadowResolution = 1024;
    QualityGovernor governor;
    runSeconds(governor, settings, kTarget * 2.0f, 30.0f);
    CHECK(settings.renderScale <= 0.8f);
    CHECK(settings.renderScale >= settings.governorMinScale - 1e-4f || settings.governorMinScale > 0.8f);
    CHECK(settings.shadowResolution >= 512);
    settings.qualityGovernor = false;
    updateGovernor(governor, settings, kTarget, kStep);
    CHECK(settings.renderScale == 0.8f);
    CHECK(settings.shadowResolution == 1024);
    CHECK(governor.level == 0);
    settings.qualityGovernor = true;
    runSeconds(governor, settings, kTarget * 2.0f, 5.0f);
    settings.renderScale = 0.6f;
    updateGovernor(governor, settings, kTarget * 2.0f, kStep);
    CHECK(governor.baseline.renderScale == 0.6f);
    CHECK(governor.level == 0);
}

void testGovernorLimitsLadder() {
    GovernorLimits baseline{1.0f, 2048, 2};
    GovernorLimits previous = governorLimitsAt(0, baseline, 0.6f);
    CHECK(previous == baseline);
    for (int level = 1; level <= kGovernorMaxLevel; ++level) {
        const GovernorLimits next = governorLimitsAt(level, baseline, 0.6f);
        CHECK(next.renderScale <= previous.renderScale);
        CHECK(next.shadowResolution <= previous.shadowResolution);
        CHECK(next.reflectionQuality <= previous.reflectionQuality);
        CHECK(next.renderScale >= 0.6f - 1e-4f);
        previous = next;
    }
    CHECK(governorLimitsAt(99, baseline, 0.6f) == governorLimitsAt(kGovernorMaxLevel, baseline, 0.6f));
    CHECK(governorLimitsAt(-3, baseline, 0.6f) == baseline);
}

}

int main() {
    testQualitySettings();
    testGovernorStaysAtFullQualityOnTarget();
    testGovernorStepsDownSlowly();
    testGovernorRecoversWithHysteresis();
    testGovernorDoesNotOscillate();
    testGovernorBaselineAndDisable();
    testGovernorLimitsLadder();
    return testsupport::finish();
}
