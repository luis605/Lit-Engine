#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "Engine/Log/Log.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.settings;
import Engine.asset;
import Engine.Portal.view;
import Engine.Instancing;

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
    const InstanceRenderStats instances = engine.instanceStats();
    out << "instancingEnabled " << instances.enabled << "\ninstanceBatches " << instances.batches << "\ninstances " << instances.instances << "\ninstancePoolCells " << instances.poolCells << "\ninstanceFrame " << instances.frame << '\n';
    auto instanceView = [&](const char* prefix, const InstanceViewStats& v) {
        out << prefix << "CellsTested " << v.cellsTested << '\n' << prefix << "CellsAccepted " << v.cellsAccepted << '\n' << prefix << "InstancesTested " << v.instancesTested << '\n' << prefix << "Visible " << v.visible << '\n' << prefix << "Occluded " << v.occluded << '\n' << prefix << "Overflow " << v.overflow << '\n';
    };
    instanceView("instanceEarly", instances.early);
    instanceView("instanceRetest", instances.retest);
    instanceView("instanceShadow", instances.shadow);
    for (size_t lod = 0; lod < instances.perLod.size(); ++lod) out << "instanceLod" << lod << ' ' << instances.perLod[lod] << '\n';
    for (const PortalViewStats& portal : engine.portalStats()) {
        out << "portalView " << (portal.label.empty() ? std::string("main") : portal.label) << " depth " << portal.depth << " drawn " << portal.drawn << " sunCascades " << portal.sunCascades << " sharedCascades " << portal.sharedCascades << '\n';
    }
}

using TimingSamples = std::vector<std::pair<std::string, std::vector<double>>>;

void sampleTimings(Engine& engine, TimingSamples& samples) {
    const auto timings = engine.gpuTimings();
    for (size_t index = 0; index < timings.size(); ++index) {
        if (index == samples.size()) samples.push_back({std::string(timings[index].name), {}});
        samples[index].second.push_back(timings[index].milliseconds);
    }
}

void writeTimings(TimingSamples& samples, const std::string& path) {
    std::ofstream out(path);
    for (auto& [name, values] : samples) {
        const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
        std::nth_element(values.begin(), middle, values.end());
        out << name << ' ' << *middle << '\n';
    }
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
    if (options.testScene == "light_stress" || options.testScene == "vsm_mixed" || options.testScene == "largeworld") setenv("LIT_FIXED_DT", "0.016666667", 0);
    if (!options.timingsFile.empty() && !std::getenv("LIT_UPLOAD_STATS")) {
        setenv("LIT_UPLOAD_STATS", "1", 0);
        setenv("LIT_UPLOAD_STATS_FILE", (options.timingsFile + ".uploads").c_str(), 0);
    }
    Engine engine;
    if (!engine.open({"LitShot", options.width, options.height})) return 1;
    engine.setFullProfiling(!options.timingsFile.empty() || !options.perfFile.empty());
    engine.setProfilingHud(false);
    buildScene(engine, options);
    if (options.hasCamera) {
        engine.world().camera().setPos(glm::vec3(options.cameraValues[0], options.cameraValues[1], options.cameraValues[2]));
        engine.world().camera().setOrientation(options.cameraValues[3], options.cameraValues[4], options.cameraValues[5]);
    }
    applySettings(engine, options);
    if (!options.perfFile.empty()) engine.enablePerfLog(options.perfFile);

    int frame = 0;
    TimingSamples timingSamples;
    engine.run([&](float) {
        ++frame;
        if (!options.perfFile.empty() && frame == options.frames) engine.disablePerfLog();
        if (!options.timingsFile.empty() && frame > options.frames / 2 && frame <= options.frames) sampleTimings(engine, timingSamples);
        if (frame == options.bakeFrame) engine.bakeReflectionProbes();
        if (frame == options.frames) engine.requestScreenshot(options.output);
        if (frame == options.frames + 1) {
            if (!options.timingsFile.empty()) writeTimings(timingSamples, options.timingsFile);
            if (!options.statsFile.empty()) writeClusterStats(engine, options.statsFile);
            glfwSetWindowShouldClose(engine.window(), GLFW_TRUE);
        }
    });
    return 0;
}
