#include <string>
#include "Engine/Log/Log.hpp"
#include "ShotOptions.hpp"

import Engine.engine;
import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.TestScenes;

void buildScene(Engine& engine, const ShotOptions& options) {
    if (options.testScene != "dense" && !options.sceneFile.empty()) {
        engine.preloadSceneMeshes(options.sceneFile);
        if (!engine.world().loadScene(options.sceneFile)) Lit::Log::Error("Could not load scene {}", options.sceneFile);
        return;
    }
    TestSceneParams params;
    params.lightIntensity = options.lightIntensity;
    params.probe = !options.noProbe;
    params.lightCount = options.lightCount;
    params.denseGrid = options.denseGrid;
    params.largeWorld.originKm = options.originKm;
    params.largeWorld.flySpeed = options.flySpeed;
    params.largeWorld.rebaseFrame = options.rebaseFrame;
    const TestSceneKit kit = makeTestSceneKit(engine);
    TestSceneInfo info;
    if (buildTestScene(engine, options.testScene, kit, info, params)) {
        applyTestSceneCamera(engine, info);
    } else {
        Lit::Log::Error("Unknown test scene {}", options.testScene);
    }
}
