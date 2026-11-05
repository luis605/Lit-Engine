module;

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

module Editor.diagnostics;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.Profiler;
import Engine.SceneValidator;
import Engine.Render.settings;
import Engine.Streaming;
import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr int kExplainWarmupFrames = 4;
constexpr size_t kTopCpuScopes = 8;
constexpr float kWindowWidth = 560.0f;
constexpr float kWindowHeight = 420.0f;

ImVec4 severityColor(Severity severity) {
    switch (severity) {
        case Severity::Error: return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        case Severity::Warning: return ImVec4(1.0f, 0.77f, 0.27f, 1.0f);
        default: return ImVec4(0.47f, 0.7f, 1.0f, 1.0f);
    }
}

ToastKind toastFor(size_t errors, size_t warnings) {
    if (errors > 0) return ToastKind::Error;
    return warnings > 0 ? ToastKind::Warning : ToastKind::Success;
}

FrameFacts collectFacts(EditorContext& ctx, float frameMs) {
    Engine& engine = ctx.engine;
    FrameFacts facts;
    facts.cpuMs = std::max(static_cast<double>(frameMs), engine.profiler().lastFrameMs());
    facts.gpuMs = engine.gpuTotalMilliseconds();
    facts.scene = gatherSceneFacts(ctx.world);
    for (const auto& timing : engine.gpuTimings()) facts.gpuPasses.emplace_back(timing.name, timing.milliseconds);
    for (const ProfileSummary& scope : engine.profiler().topScopes(kTopCpuScopes)) facts.cpuScopes.emplace_back(scope.name, scope.totalMs);
    if (StreamingSystem* streaming = engine.streaming()) {
        const StreamingStats stats = streaming->stats();
        facts.streamingPending = stats.pendingCells + stats.pendingAssets;
        facts.streamingHitches = stats.hitches;
    }
    const RenderSettings& settings = engine.renderSettings();
    facts.shadows = settings.shadows;
    facts.localShadows = settings.localShadows;
    facts.shadowResolution = settings.shadowResolution;
    facts.shadowCascades = settings.shadowCascades;
    return facts;
}

}

void Diagnostics::validate(EditorContext& ctx) {
    ValidatorLimits limits;
    limits.checkAssets = true;
    limits.meshesFromBounds = true;
    limits.materialCount = static_cast<uint32_t>(ctx.engine.materialCount());
    limits.portals = &ctx.engine.portals();
    m_issues = validateScene(ctx.world, limits);
    m_validated = true;
    m_open = true;
    const size_t errors = countIssues(m_issues, Severity::Error);
    const size_t warnings = countIssues(m_issues, Severity::Warning);
    ctx.toast(m_issues.empty() ? "Scene is valid" : std::format("Scene check: {} errors, {} warnings", errors, warnings), toastFor(errors, warnings));
}

void Diagnostics::requestExplain(EditorContext& ctx) {
    Profiler& profiler = ctx.engine.profiler();
    if (m_explainCountdown <= 0) m_restoreProfiler = !profiler.enabled();
    profiler.setEnabled(true);
    m_explainCountdown = kExplainWarmupFrames;
    m_open = true;
}

void Diagnostics::update(EditorContext& ctx, float frameMs) {
    if (m_explainCountdown <= 0) return;
    if (--m_explainCountdown > 0) return;
    explainNow(ctx, frameMs);
    if (m_restoreProfiler) ctx.engine.profiler().setEnabled(false);
    m_restoreProfiler = false;
}

void Diagnostics::explainNow(EditorContext& ctx, float frameMs) {
    m_findings = explainFrame(collectFacts(ctx, frameMs));
    ctx.toast("Frame explained: " + m_findings.front().title, ToastKind::Info);
}

void Diagnostics::drawIssues(EditorContext& ctx) {
    if (!m_validated) {
        ImGui::TextDisabled("run Validate scene to list problems");
        return;
    }
    if (m_issues.empty()) ImGui::TextColored(ImVec4(0.35f, 0.86f, 0.5f, 1.0f), "no problems found");
    for (size_t index = 0; index < m_issues.size(); ++index) {
        const ValidationIssue& issue = m_issues[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TextColored(severityColor(issue.severity), "%-7s", severityName(issue.severity));
        ImGui::SameLine();
        const bool selectable = ctx.world.isAlive(issue.entity);
        if (ImGui::Selectable(issue.message.c_str(), false, selectable ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled) && selectable) {
            ctx.select(issue.entity);
            flyTo(ctx, ctx.world.getWorldBounds(issue.entity));
        }
        ImGui::PopID();
    }
}

void Diagnostics::drawFindings() {
    if (m_explainCountdown > 0) {
        ImGui::TextDisabled("sampling the frame...");
        return;
    }
    if (m_findings.empty()) {
        ImGui::TextDisabled("run Explain this frame to see what is slow");
        return;
    }
    for (const FrameFinding& finding : m_findings) {
        ImGui::BulletText("%s", finding.title.c_str());
        ImGui::Indent();
        ImGui::TextDisabled("%s", finding.advice.c_str());
        ImGui::Unindent();
    }
}

void Diagnostics::draw(EditorContext& ctx) {
    if (!m_open) return;
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Diagnostics###DiagnosticsPanel", &m_open)) {
        if (ImGui::Button("Validate scene")) validate(ctx);
        ImGui::SameLine();
        if (ImGui::Button("Explain this frame")) requestExplain(ctx);
        if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen)) drawFindings();
        if (ImGui::CollapsingHeader("Scene problems", ImGuiTreeNodeFlags_DefaultOpen)) drawIssues(ctx);
    }
    ImGui::End();
}
