#include <GLFW/glfw3.h>
#include <fstream>
#include <string>
#include "Engine/Log/Log.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.settings;

void buildScene(Engine& engine, const ShotOptions& options);

namespace {

void applySettings(Engine& engine, const ShotOptions& options) {
    for (const auto& [key, value] : options.settings) {
        if (!setRenderSetting(engine.renderSettings(), key, value)) Lit::Log::Error("Unknown or invalid setting {}={}", key, value);
    }
}

void writeTimings(Engine& engine, const std::string& path) {
    std::ofstream out(path);
    for (const auto& timing : engine.gpuTimings()) out << timing.name << ' ' << timing.milliseconds << '\n';
}

}

int main(int argc, char** argv) {
    ShotOptions options;
    if (!parseArguments(argc, argv, options)) {
        printUsage();
        return 2;
    }
    Engine engine;
    if (!engine.open({"LitShot", options.width, options.height})) return 1;
    engine.setFullProfiling(!options.timingsFile.empty());
    engine.setProfilingHud(false);
    buildScene(engine, options);
    if (options.hasCamera) {
        engine.world().camera().setPos(glm::vec3(options.cameraValues[0], options.cameraValues[1], options.cameraValues[2]));
        engine.world().camera().setOrientation(options.cameraValues[3], options.cameraValues[4]);
    }
    applySettings(engine, options);

    int frame = 0;
    engine.run([&](float) {
        ++frame;
        if (frame == options.bakeFrame) engine.bakeReflectionProbes();
        if (frame == options.frames) engine.requestScreenshot(options.output);
        if (frame == options.frames + 1) {
            if (!options.timingsFile.empty()) writeTimings(engine, options.timingsFile);
            glfwSetWindowShouldClose(engine.window(), GLFW_TRUE);
        }
    });
    return 0;
}
