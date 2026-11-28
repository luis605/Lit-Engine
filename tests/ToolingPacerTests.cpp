#include "TestSupport.hpp"
#include "ToolingSuites.hpp"
#include "Engine/FramePacer.hpp"
#include "Engine/FramePolicy.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#define CHECK(expression) testsupport::recordCheck((expression), #expression, __LINE__)

namespace {

double settle(Lit::FramePacer& pacer, double workMs, int frames) {
    double interval = pacer.intervalMs();
    for (int i = 0; i < frames; ++i) interval = pacer.addFrame(workMs);
    return interval;
}

void writeSupply(const std::filesystem::path& root, const std::string& name, const std::string& type, int online) {
    std::filesystem::create_directories(root / name);
    std::ofstream(root / name / "type") << type << '\n';
    std::ofstream(root / name / "online") << online << '\n';
}

void runPowerPolicyTests() {
    const Lit::PowerSettings defaults;
    Lit::FrameConditions conditions;
    conditions.secondsSinceActivity = 60.0;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 0.0);
    conditions.sceneBusy = false;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 1000.0 / defaults.idleFps);
    conditions.secondsSinceActivity = 0.0;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 0.0);
    conditions.secondsSinceActivity = 60.0;
    conditions.focused = false;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 1000.0 / defaults.unfocusedIdleFps);
    conditions.sceneBusy = true;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 1000.0 / defaults.unfocusedBusyFps);
    conditions.secondsSinceActivity = 0.0;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 0.0);
    conditions.hidden = true;
    CHECK(Lit::powerIntervalMs(defaults, conditions) == 1000.0 / defaults.hiddenFps);

    Lit::PowerSettings battery;
    battery.batteryMaxFps = 40.0;
    Lit::FrameConditions playing;
    CHECK(Lit::powerIntervalMs(battery, playing) == 0.0);
    playing.onBattery = true;
    CHECK(Lit::powerIntervalMs(battery, playing) == 25.0);

    Lit::PowerSettings off;
    off.enabled = false;
    CHECK(Lit::powerIntervalMs(off, conditions) == 0.0);

    const std::map<std::string, std::string> environment{{"LIT_IDLE_FPS", "0"}, {"LIT_UNFOCUSED_FPS", "20"}, {"LIT_HIDDEN_FPS", "junk"}, {"LIT_BATTERY_MAX_FPS", "-5"}};
    const auto lookup = [&environment](const char* name) -> const char* {
        const auto found = environment.find(name);
        return found == environment.end() ? nullptr : found->second.c_str();
    };
    const Lit::PowerSettings read = Lit::PowerSettings::fromEnvironment(lookup);
    CHECK(read.enabled);
    CHECK(read.idleFps == 0.0);
    CHECK(read.unfocusedBusyFps == 20.0);
    CHECK(read.hiddenFps == defaults.hiddenFps);
    CHECK(read.batteryMaxFps == 0.0);
    CHECK(!Lit::PowerSettings::fromEnvironment([](const char* name) -> const char* { return std::string(name) == "LIT_POWER_SAVING" ? "0" : nullptr; }).enabled);

    const std::filesystem::path supplies = std::filesystem::temp_directory_path() / "lit_power_supply_test";
    std::filesystem::remove_all(supplies);
    CHECK(!Lit::runningOnBattery(supplies));
    writeSupply(supplies, "BAT1", "Battery", 0);
    CHECK(!Lit::runningOnBattery(supplies));
    writeSupply(supplies, "ACAD", "Mains", 0);
    CHECK(Lit::runningOnBattery(supplies));
    writeSupply(supplies, "USB", "Mains", 1);
    CHECK(!Lit::runningOnBattery(supplies));
    std::filesystem::remove_all(supplies);
}

}

void runPacerTests() {
    runPowerPolicyTests();
    Lit::FramePacer off;
    CHECK(!off.enabled());
    CHECK(settle(off, 20.0, 300) == 0.0);

    Lit::FramePacer pacer;
    pacer.setMaxFps(60.0);
    CHECK(std::abs(pacer.intervalMs() - 1000.0 / 60.0) < 1e-9);
    CHECK(std::abs(settle(pacer, 5.0, 600) - 1000.0 / 60.0) < 1e-9);
    const double loaded = settle(pacer, 18.0, 600);
    CHECK(std::abs(loaded - 18.0 * Lit::FramePacer::kHeadroom) < 1e-9);
    CHECK(settle(pacer, 18.1, 600) == loaded);
    CHECK(std::abs(settle(pacer, 500.0, 600) - 1000.0 / Lit::FramePacer::kSlowestFps) < 1e-9);
    const double afterOneUpdate = settle(pacer, 5.0, static_cast<int>(Lit::FramePacer::kWindow + Lit::FramePacer::kUpdateEvery));
    CHECK(afterOneUpdate > 1000.0 / 60.0);
    CHECK(std::abs(settle(pacer, 5.0, 3000) - 1000.0 / 60.0) < 1e-9);

    Lit::FramePacer starting;
    starting.setMaxFps(60.0);
    CHECK(settle(starting, 500.0, static_cast<int>(Lit::FramePacer::kWarmup)) == 1000.0 / 60.0);
    CHECK(settle(starting, 5.0, 600) == 1000.0 / 60.0);

    Lit::FramePacer spiky;
    spiky.setMaxFps(60.0);
    for (int i = 0; i < 600; ++i) spiky.addFrame(i % 40 == 0 ? 90.0 : 5.0);
    CHECK(std::abs(spiky.intervalMs() - 1000.0 / 60.0) < 1e-9);
}
