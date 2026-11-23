#include <GLFW/glfw3.h>
#include <cstdlib>
#include <fstream>
#include <string>
#include "Engine/Log/Log.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.settings;
import Engine.asset;
import Engine.Portal.view;

void buildScene(Engine& engine, const ShotOptions& options);

namespace {

void applySettings(Engine& engine, const ShotOptions& options) {
    for (const auto& [key, value] : options.settings) {
        if (!setRenderSetting(engine.renderSettings(), key, value)) Lit::Log::Error("Unknown or invalid setting {}={}", key, value);
    }
}

void writeClusterStats(Engine& engine, const std::string& path) {
    const ClusterRenderStats stats = engine.clusterStats();
    std::ofstream out(path);
    out << "enabled " << stats.enabled << "\nclusterMeshes " << stats.clusterMeshes << "\nresidentClusters " << stats.residentClusters << "\nresidentTriangles " << stats.residentTriangles << "\nframe " << stats.frame << '\n';
    auto view = [&](const char* prefix, const ClusterViewStats& v) {
        out << prefix << "Instances " << v.instances << '\n' << prefix << "NodesTested " << v.nodesTested << '\n' << prefix << "WorkItems " << v.workItems << '\n' << prefix << "ClustersTested " << v.clustersTested << '\n' << prefix << "ClustersSelected " << v.clustersSelected << '\n'
            << prefix << "ClustersDrawn " << v.clustersDrawn << '\n' << prefix << "Triangles " << v.trianglesDrawn << '\n' << prefix << "Draws " << v.draws << '\n' << prefix << "Overflows " << v.overflows << '\n';
    };
    view("camera", stats.camera);
    view("shadow", stats.shadow);
    for (const PortalViewStats& portal : engine.portalStats()) {
        out << "portalView " << (portal.label.empty() ? std::string("main") : portal.label) << " depth " << portal.depth << " drawn " << portal.drawn << " sunCascades " << portal.sunCascades << " sharedCascades " << portal.sharedCascades << '\n';
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
    if (options.deterministic) {
        setenv("LIT_FIXED_DT", "0.016666667", 1);
        setenv("LIT_FIXED_WINDOW", "1", 1);
    }
    // Animated scenes (testscene.* systems) advance by world time: a fixed step makes frame N identical from run to run.
    if (options.testScene == "light_stress" || options.testScene == "vsm_mixed" || options.testScene == "largeworld") setenv("LIT_FIXED_DT", "0.016666667", 0);
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
            if (!options.statsFile.empty()) writeClusterStats(engine, options.statsFile);
            glfwSetWindowShouldClose(engine.window(), GLFW_TRUE);
        }
    });
    return 0;
}
