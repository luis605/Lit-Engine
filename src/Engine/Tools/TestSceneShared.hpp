#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct TestSceneShared {
    std::vector<uint32_t> materialSlots;
    size_t materialCursor = 0;
    std::map<std::string, uint32_t> meshes;
    std::map<std::string, uint32_t> dynamicSources;
    std::map<uint32_t, std::vector<uint32_t>> freeInstances;
    std::vector<std::pair<uint32_t, uint32_t>> usedInstances;
    bool settingsSaved = false;
    float timeOfDay = 0.0f;
    float sunAzimuth = 0.0f;
    float cloudCoverage = 0.0f;
    float cloudSpeed = 0.0f;
    bool clouds = false;
    bool fog = false;
    int reflectionMode = 0;
    float sunIlluminance = 0.0f;
    float manualEV100 = 0.0f;
    float lightScatter = 0.0f;
    bool autoExposure = true;
    std::shared_ptr<void> largeWorld;
};
