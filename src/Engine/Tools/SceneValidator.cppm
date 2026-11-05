module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module Engine.SceneValidator;

import Engine.World;
import Engine.Portal;
import Engine.Render.entity;

export enum class Severity { Info, Warning, Error };

export struct ValidationIssue {
    Severity severity = Severity::Info;
    std::string code;
    std::string message;
    EntityHandle entity;
};

export struct ValidatorLimits {
    uint32_t meshCount = 0;
    uint32_t materialCount = 0;
    bool checkAssets = false;
    bool meshesFromBounds = false;
    float minScale = 1.0e-4f;
    float maxScale = 1.0e4f;
    size_t maxIssues = 500;
    const PortalSystem* portals = nullptr;
};

export struct SceneFacts {
    size_t entities = 0;
    size_t drawables = 0;
    size_t transparent = 0;
    size_t shadowCasters = 0;
    size_t lights = 0;
    size_t spotAndPointLights = 0;
};

export struct FrameFacts {
    double cpuMs = 0.0;
    double gpuMs = 0.0;
    double targetMs = 16.6;
    SceneFacts scene;
    std::vector<std::pair<std::string, double>> gpuPasses;
    std::vector<std::pair<std::string, double>> cpuScopes;
    uint32_t streamingPending = 0;
    uint64_t streamingHitches = 0;
    bool shadows = true;
    bool localShadows = true;
    int shadowResolution = 2048;
    int shadowCascades = 4;
};

export struct FrameFinding {
    double weight = 0.0;
    std::string title;
    std::string advice;
};

export [[nodiscard]] const char* severityName(Severity severity);
export [[nodiscard]] std::vector<ValidationIssue> validateScene(World& world, const ValidatorLimits& limits = {});
export [[nodiscard]] size_t countIssues(const std::vector<ValidationIssue>& issues, Severity severity);
export [[nodiscard]] SceneFacts gatherSceneFacts(World& world);
export [[nodiscard]] std::vector<FrameFinding> explainFrame(const FrameFacts& facts);
export [[nodiscard]] std::string formatFindings(const std::vector<FrameFinding>& findings);
