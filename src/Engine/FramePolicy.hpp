#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace Lit {

struct PowerSettings {
    bool enabled = true;
    double idleFps = 10.0;
    double unfocusedIdleFps = 5.0;
    double unfocusedBusyFps = 30.0;
    double hiddenFps = 4.0;
    double batteryMaxFps = 0.0;
    double activitySeconds = 2.0;

    template <typename Lookup>
    static PowerSettings fromEnvironment(Lookup lookup) {
        PowerSettings settings;
        const auto read = [&lookup](const char* name, double& value) {
            const char* text = lookup(name);
            if (!text) return;
            char* end = nullptr;
            const double parsed = std::strtod(text, &end);
            if (end != text && parsed >= 0.0) value = parsed;
        };
        if (const char* text = lookup("LIT_POWER_SAVING")) settings.enabled = std::string_view(text) != "0";
        read("LIT_IDLE_FPS", settings.idleFps);
        read("LIT_UNFOCUSED_IDLE_FPS", settings.unfocusedIdleFps);
        read("LIT_UNFOCUSED_FPS", settings.unfocusedBusyFps);
        read("LIT_HIDDEN_FPS", settings.hiddenFps);
        read("LIT_BATTERY_MAX_FPS", settings.batteryMaxFps);
        read("LIT_ACTIVITY_SECONDS", settings.activitySeconds);
        return settings;
    }
};

struct FrameConditions {
    bool focused = true;
    bool hidden = false;
    bool onBattery = false;
    bool sceneBusy = true;
    double secondsSinceActivity = 0.0;
};

inline bool runningOnBattery(const std::filesystem::path& supplies) {
    bool sawMains = false;
    std::error_code error;
    for (std::filesystem::directory_iterator it(supplies, error), end; !error && it != end; it.increment(error)) {
        std::string type;
        std::ifstream(it->path() / "type") >> type;
        if (type != "Mains") continue;
        int online = 0;
        if (std::ifstream(it->path() / "online") >> online && online != 0) return false;
        sawMains = true;
    }
    return sawMains;
}

inline double intervalForFps(double fps) { return fps > 0.0 ? 1000.0 / fps : 0.0; }

inline double powerIntervalMs(const PowerSettings& settings, const FrameConditions& conditions) {
    if (!settings.enabled) return 0.0;
    const double batteryFloor = conditions.onBattery ? intervalForFps(settings.batteryMaxFps) : 0.0;
    if (conditions.hidden) return std::max(batteryFloor, intervalForFps(settings.hiddenFps));
    if (conditions.secondsSinceActivity < settings.activitySeconds) return batteryFloor;
    double fps = 0.0;
    if (conditions.sceneBusy) fps = conditions.focused ? 0.0 : settings.unfocusedBusyFps;
    else fps = conditions.focused ? settings.idleFps : settings.unfocusedIdleFps;
    return std::max(batteryFloor, intervalForFps(fps));
}

}
