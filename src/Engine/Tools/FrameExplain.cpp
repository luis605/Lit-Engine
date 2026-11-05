module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

module Engine.SceneValidator;

namespace {

constexpr double kDominantShare = 0.3;
constexpr size_t kManyDrawables = 20000;
constexpr size_t kManyCasters = 5000;
constexpr size_t kManyLights = 256;
constexpr size_t kManyTransparent = 500;
constexpr int kHeavyShadowResolution = 4096;
constexpr int kHeavyCascades = 4;
constexpr double kDrawWeightPerThousand = 0.05;
constexpr double kStallWeight = 3.0;
constexpr double kShadowSettingWeight = 1.0;
constexpr double kPercent = 100.0;

struct PassAdvice {
    const char* keyword;
    const char* advice;
};

constexpr PassAdvice kPassAdvice[] = {
    {"shadow", "lower shadowResolution or shadowCascades, shorten shadowDistance, or disable localShadows"},
    {"ssr", "use a cheaper reflection mode or lower the reflection ray count"},
    {"reflect", "use a cheaper reflection mode or lower the reflection ray count"},
    {"gtao", "reduce the ambient occlusion sample count or radius"},
    {"ao", "reduce the ambient occlusion sample count or radius"},
    {"gi", "lower global illumination quality or resolution"},
    {"bloom", "reduce bloom iterations"},
    {"light", "reduce the number of lights touching large areas, or lower light ranges"},
    {"cull", "reduce draw count with LOD or larger small-object thresholds"},
    {"portal", "lower portal maxDepth or maxViews"},
};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
    return text;
}

std::string adviceFor(const std::string& pass) {
    const std::string key = lower(pass);
    for (const PassAdvice& entry : kPassAdvice) {
        if (key.find(entry.keyword) != std::string::npos) return entry.advice;
    }
    return "inspect this pass in the profiler and reduce its inputs";
}

void addTopPass(const FrameFacts& facts, std::vector<FrameFinding>& out) {
    if (facts.gpuPasses.empty() || facts.gpuMs <= 0.0) return;
    const auto top = std::max_element(facts.gpuPasses.begin(), facts.gpuPasses.end(), [](const auto& left, const auto& right) { return left.second < right.second; });
    if (top->second < facts.gpuMs * kDominantShare) return;
    out.push_back({top->second, std::format("GPU pass '{}' takes {:.2f} ms ({:.0f}% of GPU)", top->first, top->second, kPercent * top->second / facts.gpuMs), adviceFor(top->first)});
}

void addTopScope(const FrameFacts& facts, std::vector<FrameFinding>& out) {
    if (facts.cpuScopes.empty() || facts.cpuMs <= 0.0) return;
    const auto top = std::max_element(facts.cpuScopes.begin(), facts.cpuScopes.end(), [](const auto& left, const auto& right) { return left.second < right.second; });
    if (top->second < facts.cpuMs * kDominantShare) return;
    out.push_back({top->second, std::format("CPU scope '{}' takes {:.2f} ms ({:.0f}% of the frame)", top->first, top->second, kPercent * top->second / facts.cpuMs), "reduce the work in this system or move it to jobs"});
}

void addSceneFindings(const FrameFacts& facts, std::vector<FrameFinding>& out) {
    const SceneFacts& scene = facts.scene;
    if (scene.drawables > kManyDrawables) out.push_back({kDrawWeightPerThousand * static_cast<double>(scene.drawables) / 1000.0, std::format("{} visible draws", scene.drawables), "merge static meshes, use instancing, and enable LOD or a larger small-object threshold"});
    if (facts.shadows && scene.shadowCasters > kManyCasters) out.push_back({kDrawWeightPerThousand * static_cast<double>(scene.shadowCasters) / 1000.0, std::format("{} shadow casters", scene.shadowCasters), "shorten shadowDistance, lower shadowCascades, or mark small props as non-casters"});
    if (scene.lights > kManyLights) out.push_back({kShadowSettingWeight, std::format("{} lights", scene.lights), "merge or remove lights and reduce their ranges"});
    if (scene.transparent > kManyTransparent) out.push_back({kShadowSettingWeight, std::format("{} transparent draws, high overdraw risk", scene.transparent), "use masked or opaque materials where possible and shrink large transparent quads"});
    if (facts.shadows && facts.shadowResolution >= kHeavyShadowResolution && facts.shadowCascades >= kHeavyCascades) out.push_back({kShadowSettingWeight, std::format("shadow maps are {}x{} with {} cascades", facts.shadowResolution, facts.shadowResolution, facts.shadowCascades), "drop shadowResolution to 2048 or use fewer cascades"});
    if (facts.streamingPending > 0 || facts.streamingHitches > 0) out.push_back({kStallWeight, std::format("streaming: {} pending items, {} hitches", facts.streamingPending, facts.streamingHitches), "lower the streaming budget per frame or enlarge the prefetch radius"});
}

}

std::vector<FrameFinding> explainFrame(const FrameFacts& facts) {
    std::vector<FrameFinding> findings;
    addTopPass(facts, findings);
    addTopScope(facts, findings);
    addSceneFindings(facts, findings);
    std::stable_sort(findings.begin(), findings.end(), [](const FrameFinding& left, const FrameFinding& right) { return left.weight > right.weight; });
    if (findings.empty()) {
        const double frame = std::max(facts.cpuMs, facts.gpuMs);
        findings.push_back({0.0, std::format("no dominant cost found ({:.2f} ms against a {:.2f} ms target)", frame, facts.targetMs), frame > facts.targetMs ? "enable the quality governor or lower the render scale" : "the frame is within budget"});
    }
    return findings;
}

std::string formatFindings(const std::vector<FrameFinding>& findings) {
    std::string text;
    for (const FrameFinding& finding : findings) text += "- " + finding.title + ": " + finding.advice + "\n";
    return text;
}
