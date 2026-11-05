module;

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <vector>

module Editor.profilerpanel;

import Editor.context;
import Engine.engine;
import Engine.Profiler;

namespace {

constexpr float kWindowWidth = 560.0f;
constexpr float kWindowHeight = 520.0f;
constexpr float kPlotHeight = 70.0f;
constexpr float kRowHeight = 18.0f;
constexpr float kFlameRows = 8.0f;
constexpr float kMinZoom = 1.0f;
constexpr float kMaxZoom = 32.0f;
constexpr float kMinBarWidth = 1.0f;
constexpr float kTargetMs = 16.6f;
constexpr float kTextPadding = 3.0f;
constexpr uint32_t kHueSeed = 2166136261u;
constexpr uint32_t kHuePrime = 16777619u;
constexpr float kHueRange = 360.0f;

ImU32 colorFor(const std::string& name, bool selected) {
    uint32_t hash = kHueSeed;
    for (const char letter : name) hash = (hash ^ static_cast<unsigned char>(letter)) * kHuePrime;
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    ImGui::ColorConvertHSVtoRGB(static_cast<float>(hash % static_cast<uint32_t>(kHueRange)) / kHueRange, selected ? 0.35f : 0.55f, selected ? 1.0f : 0.78f, red, green, blue);
    return IM_COL32(static_cast<int>(red * 255.0f), static_cast<int>(green * 255.0f), static_cast<int>(blue * 255.0f), 255);
}

}

void ProfilerPanel::drawHistory() {
    m_plot.assign(m_frameHistory.begin(), m_frameHistory.end());
    if (m_plot.empty()) return;
    const float peak = std::max(*std::max_element(m_plot.begin(), m_plot.end()), kTargetMs * 1.5f);
    ImGui::PlotLines("##frames", m_plot.data(), static_cast<int>(m_plot.size()), 0, std::format("frame time (peak {:.1f} ms)", peak).c_str(), 0.0f, peak, ImVec2(-1.0f, kPlotHeight));
    const ImVec2 low = ImGui::GetItemRectMin();
    const ImVec2 high = ImGui::GetItemRectMax();
    const float lineY = high.y - (high.y - low.y) * (kTargetMs / peak);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(low.x, lineY), ImVec2(high.x, lineY), IM_COL32(90, 220, 130, 160));
}

void ProfilerPanel::drawFlame() {
    ImGui::SliderFloat("zoom", &m_zoom, kMinZoom, kMaxZoom, "%.1fx", ImGuiSliderFlags_Logarithmic);
    if (m_snapshot.records.empty()) {
        ImGui::TextDisabled("no CPU scopes recorded yet");
        return;
    }
    double base = m_snapshot.records.front().startMs;
    double span = 0.0;
    uint32_t maxDepth = 0;
    for (const ProfileRecord& record : m_snapshot.records) {
        base = std::min(base, record.startMs);
        span = std::max(span, record.startMs + record.durationMs);
        maxDepth = std::max(maxDepth, record.depth);
    }
    span = std::max(span - base, 1.0e-3);
    const float height = std::max(static_cast<float>(maxDepth + 1), kFlameRows) * kRowHeight;
    ImGui::BeginChild("##flame", ImVec2(-1.0f, height + ImGui::GetStyle().ScrollbarSize), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f) * m_zoom;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int hovered = -1;
    for (size_t index = 0; index < m_snapshot.records.size(); ++index) {
        const ProfileRecord& record = m_snapshot.records[index];
        const float left = origin.x + static_cast<float>((record.startMs - base) / span) * width;
        const float right = left + std::max(static_cast<float>(record.durationMs / span) * width, kMinBarWidth);
        const float top = origin.y + static_cast<float>(record.depth) * kRowHeight;
        const ImVec2 low(left, top);
        const ImVec2 high(right, top + kRowHeight - 1.0f);
        draw->AddRectFilled(low, high, colorFor(record.name, static_cast<int>(index) == m_selected));
        if (right - left > ImGui::CalcTextSize("..").x + kTextPadding * 2.0f) {
            draw->PushClipRect(low, high, true);
            draw->AddText(ImVec2(left + kTextPadding, top + 1.0f), IM_COL32(15, 15, 20, 255), record.name.c_str());
            draw->PopClipRect();
        }
        if (ImGui::IsMouseHoveringRect(low, high)) hovered = static_cast<int>(index);
    }
    ImGui::Dummy(ImVec2(width, height));
    if (hovered >= 0) {
        const ProfileRecord& record = m_snapshot.records[static_cast<size_t>(hovered)];
        ImGui::SetTooltip("%s\n%.3f ms  (depth %u)", record.name.c_str(), record.durationMs, record.depth);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_selected = hovered;
    }
    ImGui::EndChild();
    if (m_selected >= 0) {
        const ProfileRecord& record = m_snapshot.records[static_cast<size_t>(m_selected)];
        ImGui::Text("selected: %s  %.3f ms  (%.0f%% of frame)", record.name.c_str(), record.durationMs, 100.0 * record.durationMs / span);
    }
}

void ProfilerPanel::drawGpu() {
    ImGui::Text("GPU total %.2f ms", m_snapshot.gpuMs);
    if (!ImGui::BeginTable("gpu", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    auto passes = m_snapshot.gpuPasses;
    std::sort(passes.begin(), passes.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    for (const auto& [name, milliseconds] : passes) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f ms", milliseconds);
    }
    ImGui::EndTable();
}

void ProfilerPanel::drawSystems() {
    if (!ImGui::BeginTable("systems", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    for (const auto& [name, milliseconds] : m_snapshot.systems) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f ms", milliseconds);
    }
    ImGui::EndTable();
}

void ProfilerPanel::draw(EditorContext& ctx) {
    if (!m_open) return;
    ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight), ImGuiCond_FirstUseEver);
    bool keepOpen = true;
    if (ImGui::Begin("Profiler###ProfilerPanel", &keepOpen)) {
        ImGui::Checkbox("pause / inspect", &m_paused);
        ImGui::SameLine();
        if (ImGui::Button("Export JSON")) {
            const std::string path = "resources/profile.json";
            if (exportJson(path)) ctx.toast("Profile written to " + path, ToastKind::Success);
            else ctx.toast("Could not write " + path, ToastKind::Error);
        }
        ImGui::SameLine();
        ImGui::Text("cpu %.2f ms   frame %llu", m_snapshot.cpuMs, static_cast<unsigned long long>(m_snapshot.frame));
        drawHistory();
        if (ImGui::CollapsingHeader("CPU flame graph", ImGuiTreeNodeFlags_DefaultOpen)) drawFlame();
        if (ImGui::CollapsingHeader("GPU passes", ImGuiTreeNodeFlags_DefaultOpen)) drawGpu();
        if (ImGui::CollapsingHeader("Per-system cost")) drawSystems();
    }
    ImGui::End();
    if (keepOpen) return;
    m_open = false;
    if (!m_enabledProfiler) return;
    ctx.engine.profiler().setEnabled(false);
    m_enabledProfiler = false;
}
