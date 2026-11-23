#pragma once

// Per-Engine bookkeeping shared by the test scene builders (Engine.TestScenes implementation units only).
// Everything the renderer cannot release (material slots, dynamic mesh sources / instances, big cluster meshes) is reused across
// rebuilds, so switching scenes repeatedly in the Editor does not grow GPU state.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct TestSceneShared {
    // Material slots: slot n of every build reuses the same engine material id (the first build creates them).
    std::vector<uint32_t> materialSlots;
    size_t materialCursor = 0;
    // Uploaded meshes built procedurally (cluster rocks), by key.
    std::map<std::string, uint32_t> meshes;
    // Dynamic (skinned / deformed) mesh sources by key; instances are handed out, then returned on leaveTestScene.
    std::map<std::string, uint32_t> dynamicSources;
    std::map<uint32_t, std::vector<uint32_t>> freeInstances;
    std::vector<std::pair<uint32_t, uint32_t>> usedInstances;  // (source, instance)
    // Render settings the scenes override, captured the first time a scene is built and restored on every later build.
    bool settingsSaved = false;
    float timeOfDay = 0.0f;
    float sunAzimuth = 0.0f;
    float cloudCoverage = 0.0f;
    float cloudSpeed = 0.0f;
    bool clouds = false;
    bool fog = false;
    int reflectionMode = 0;
    // Large-world scene state (null unless that scene is active).
    std::shared_ptr<void> largeWorld;
};
