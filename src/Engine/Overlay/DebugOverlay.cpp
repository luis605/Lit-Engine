module;

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

module Engine.Overlay;

import Engine.engine;
import Engine.Observation;
import Engine.AI;
import Engine.Params;
import Engine.Portal;
import Engine.Portal.view;
import Engine.World;
import Engine.glm;
import Engine.Profiler;
import Engine.renderer;
import Engine.Render.entity;

namespace {

constexpr size_t kHistoryLength = 240;
constexpr int kTabCount = 5;
constexpr size_t kMaxListedEntities = 64;
constexpr size_t kMaxTraceEvents = 40;
constexpr int kCurveSegments = 96;
constexpr float kTwoPi = 6.2831853f;
constexpr float kMinSpan = 1e-4f;
constexpr float kFrameGraphMaxMs = 33.0f;
constexpr ImU32 kPanelColor = IM_COL32(18, 20, 26, 255);

void pushSample(std::deque<float>& values, float value) {
    values.push_back(value);
    while (values.size() > kHistoryLength) values.pop_front();
}

void plotHistory(const char* label, const std::deque<float>& values, float low, float high, ImVec2 size) {
    if (values.empty()) return;
    const std::vector<float> flat(values.begin(), values.end());
    ImGui::PlotLines(label, flat.data(), static_cast<int>(flat.size()), 0, nullptr, low, high, size);
}

ImU32 stateColor(ObservationState state) {
    if (state == ObservationState::Attended) return IM_COL32(110, 170, 255, 255);
    if (state == ObservationState::Seen) return IM_COL32(230, 200, 110, 255);
    return IM_COL32(140, 140, 150, 255);
}

const char* stateName(ObservationState state) {
    if (state == ObservationState::Attended) return "Attended";
    if (state == ObservationState::Seen) return "Seen";
    return "Unobserved";
}

const char* cellNameOrDash(const std::vector<PortalCell>& cells, int cellIndex) {
    if (cellIndex < 0 || static_cast<size_t>(cellIndex) >= cells.size()) return "-";
    return cells[static_cast<size_t>(cellIndex)].name.c_str();
}

int partnerCell(const std::vector<Portal>& portals, const Portal& portal) {
    if (portal.partner < 0 || static_cast<size_t>(portal.partner) >= portals.size()) return -1;
    return portals[static_cast<size_t>(portal.partner)].cell;
}

void drawPortalGraph(const std::vector<PortalCell>& cells, const std::vector<Portal>& portals, int cameraCell) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 200.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kPanelColor);
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const float radius = std::min(size.x, size.y) * 0.5f - 24.0f;
    std::vector<ImVec2> nodes(cells.size());
    for (size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
        const float angle = cells.size() == 1 ? 0.0f : kTwoPi * static_cast<float>(cellIndex) / static_cast<float>(cells.size());
        nodes[cellIndex] = ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
    }
    for (size_t portalIndex = 0; portalIndex < portals.size(); ++portalIndex) {
        const Portal& portal = portals[portalIndex];
        const int otherCell = partnerCell(portals, portal);
        if (portal.cell < 0 || otherCell < 0) continue;
        if (static_cast<size_t>(portal.cell) >= cells.size() || static_cast<size_t>(otherCell) >= cells.size()) continue;
        if (static_cast<int>(portalIndex) >= portal.partner) continue;
        drawList->AddLine(nodes[static_cast<size_t>(portal.cell)], nodes[static_cast<size_t>(otherCell)], IM_COL32(90, 140, 210, 200), 1.5f);
    }
    for (size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
        const bool isCameraCell = static_cast<int>(cellIndex) == cameraCell;
        drawList->AddCircleFilled(nodes[cellIndex], isCameraCell ? 9.0f : 6.0f, isCameraCell ? IM_COL32(255, 210, 90, 255) : IM_COL32(150, 190, 240, 255));
        drawList->AddText(ImVec2(nodes[cellIndex].x + 10.0f, nodes[cellIndex].y - 7.0f), IM_COL32(220, 224, 232, 255), cells[cellIndex].name.c_str());
    }
    ImGui::Dummy(size);
}

void drawParamTable(const std::vector<ParamInfo>& params, std::string& selectedParam) {
    if (!ImGui::BeginTable("params", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 150.0f))) return;
    ImGui::TableSetupColumn("name");
    ImGui::TableSetupColumn("value");
    ImGui::TableSetupColumn("target");
    ImGui::TableSetupColumn("slot");
    ImGui::TableSetupColumn("src");
    ImGui::TableHeadersRow();
    for (const ParamInfo& param : params) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Selectable(param.name.c_str(), selectedParam == param.name, ImGuiSelectableFlags_SpanAllColumns)) selectedParam = param.name;
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", param.value);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", param.target);
        ImGui::TableNextColumn();
        ImGui::Text("%u", param.slot);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(param.derived ? param.input.c_str() : "set");
    }
    ImGui::EndTable();
}

void drawParamCurve(ParamBus& bus, const ParamInfo& selected, const ParamCurve& curve) {
    ImGui::Text("curve %s -> %s", selected.input.c_str(), selected.name.c_str());
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 110.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), kPanelColor);
    const float inputMin = curve.points.front().first;
    const float inputMax = curve.points.back().first;
    const float inputSpan = std::max(inputMax - inputMin, kMinSpan);
    float outputMin = curve.points.front().second;
    float outputMax = outputMin;
    for (const auto& [inputValue, outputValue] : curve.points) {
        outputMin = std::min(outputMin, outputValue);
        outputMax = std::max(outputMax, outputValue);
    }
    const float outputSpan = std::max(outputMax - outputMin, kMinSpan);
    auto toScreen = [&](float inputValue, float outputValue) {
        return ImVec2(origin.x + 6.0f + (inputValue - inputMin) / inputSpan * (size.x - 12.0f), origin.y + size.y - 6.0f - (outputValue - outputMin) / outputSpan * (size.y - 12.0f));
    };
    ImVec2 previous = toScreen(inputMin, curve.eval(inputMin));
    for (int segment = 1; segment <= kCurveSegments; ++segment) {
        const float inputValue = inputMin + inputSpan * static_cast<float>(segment) / static_cast<float>(kCurveSegments);
        const ImVec2 next = toScreen(inputValue, curve.eval(inputValue));
        drawList->AddLine(previous, next, IM_COL32(120, 190, 255, 255), 2.0f);
        previous = next;
    }
    for (const auto& [inputValue, outputValue] : curve.points) drawList->AddCircleFilled(toScreen(inputValue, outputValue), 3.5f, IM_COL32(255, 255, 255, 255));
    if (bus.has(selected.input)) {
        const float liveInput = std::clamp(bus.get(selected.input), inputMin, inputMax);
        const ImVec2 live = toScreen(liveInput, curve.eval(liveInput));
        drawList->AddLine(ImVec2(live.x, origin.y), ImVec2(live.x, origin.y + size.y), IM_COL32(255, 200, 80, 140));
        drawList->AddCircleFilled(live, 5.0f, IM_COL32(255, 200, 80, 255));
    }
    ImGui::Dummy(size);
}

void drawTraceEvent(const TraceEvent& event, bool openByDefault) {
    const bool open = ImGui::TreeNodeEx("event", openByDefault ? ImGuiTreeNodeFlags_DefaultOpen : 0, "t%llu a%u %s [%s] -> %s", static_cast<unsigned long long>(event.tick), event.agent, event.node.c_str(), event.kind.c_str(), event.winner.c_str());
    if (!open) return;
    if (!event.reason.empty()) ImGui::TextDisabled("%s", event.reason.c_str());
    for (const TraceOption& option : event.options) {
        const bool isWinner = option.name == event.winner;
        if (isWinner) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 220, 140, 255));
        ImGui::Text("%s%s  %.3f%s", isWinner ? "> " : "  ", option.name.c_str(), option.score, option.available ? "" : "  (unavailable)");
        if (isWinner) ImGui::PopStyleColor();
        for (const TraceConsideration& consideration : option.considerations) ImGui::Text("      %s  value %.3f  score %.3f", consideration.input.c_str(), consideration.value, consideration.score);
    }
    ImGui::TreePop();
}

void drawGpuPassTable(const std::vector<GpuPassTiming>& timings, double totalMilliseconds) {
    if (!ImGui::BeginTable("gpu", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("pass");
    ImGui::TableSetupColumn("ms");
    ImGui::TableSetupColumn("share");
    ImGui::TableHeadersRow();
    const double total = std::max(totalMilliseconds, 1e-6);
    for (const GpuPassTiming& timing : timings) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(timing.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", timing.milliseconds);
        ImGui::TableNextColumn();
        ImGui::ProgressBar(static_cast<float>(timing.milliseconds / total), ImVec2(-1.0f, 0.0f), "");
    }
    ImGui::EndTable();
}

}

const std::deque<float>& DebugOverlay::history(const std::string& param) const {
    static const std::deque<float> empty;
    const auto found = m_paramHistory.find(param);
    return found == m_paramHistory.end() ? empty : found->second;
}

void DebugOverlay::sample(Engine& engine) {
    const auto now = std::chrono::steady_clock::now();
    if (m_sampled) pushSample(m_frameMs, std::chrono::duration<float, std::milli>(now - m_lastSample).count());
    m_lastSample = now;
    m_sampled = true;
    pushSample(m_gpuMs, static_cast<float>(engine.gpuTotalMilliseconds()));
    for (const ParamInfo& param : engine.params().params()) pushSample(m_paramHistory[param.name], param.value);
}

void DebugOverlay::draw(Engine& engine) {
    if (!m_visible) return;
    sample(engine);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 500.0f, viewport->WorkPos.y + 60.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if (!ImGui::Begin("Debug overlay", &m_visible)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("debug_tabs")) {
        const char* const tabNames[kTabCount] = {"Portals", "Params", "Observation", "AI", "Perf"};
        for (int tab = 0; tab < kTabCount; ++tab) {
            const bool jump = m_requestedTab == tab;
            if (!ImGui::BeginTabItem(tabNames[tab], nullptr, jump ? ImGuiTabItemFlags_SetSelected : 0)) continue;
            if (tab == 0) drawPortals(engine);
            else if (tab == 1) drawParams(engine);
            else if (tab == 2) drawObservation(engine);
            else if (tab == 3) drawTrace(engine);
            else drawPerf(engine);
            ImGui::EndTabItem();
        }
        m_requestedTab = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void DebugOverlay::drawPortals(Engine& engine) {
    PortalSystem& portalSystem = engine.portals();
    const auto& cells = portalSystem.cells();
    const auto& portals = portalSystem.portals();
    const int cameraCell = portalSystem.cameraCell();
    ImGui::Text("%zu cells  %zu portals  camera cell %s", cells.size(), portals.size(), cellNameOrDash(cells, cameraCell));
    PortalSettings& settings = portalSystem.settings();
    ImGui::SliderInt("Max depth", &settings.maxDepth, 0, 8);
    ImGui::SliderInt("Max views", &settings.maxViews, 1, 64);
    drawPortalGraph(cells, portals, cameraCell);
    if (ImGui::CollapsingHeader("Portals", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const Portal& portal : portals) ImGui::BulletText("%s  %s -> %s", portal.name.c_str(), cellNameOrDash(cells, portal.cell), cellNameOrDash(cells, partnerCell(portals, portal)));
    }
    if (ImGui::CollapsingHeader("Views", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& stats = engine.portalStats();
        if (stats.empty()) ImGui::TextDisabled("no portal views this frame");
        for (const PortalViewStats& view : stats) ImGui::Text("%*s%s  drawn %u  %.3f ms", view.depth * 2, "", view.label.c_str(), view.drawn, view.gpuMilliseconds);
    }
}

void DebugOverlay::drawParams(Engine& engine) {
    ParamBus& bus = engine.params();
    const auto& params = bus.params();
    ImGui::Text("%zu params  file %s  reloads %u", params.size(), bus.watchedPath().empty() ? "-" : bus.watchedPath().c_str(), bus.reloadCount());
    const auto files = engine.watchedFiles();
    ImGui::Text("watched data files %zu  data reloads %u", files.size(), engine.dataReloads());
    drawParamTable(params, m_selectedParam);
    const ParamInfo* selected = nullptr;
    for (const ParamInfo& param : params) {
        if (param.name == m_selectedParam) {
            selected = &param;
            break;
        }
    }
    if (!selected && !params.empty()) {
        selected = &params.front();
        m_selectedParam = selected->name;
    }
    if (!selected) return;
    float low = 0.0f;
    float high = 1.0f;
    const std::deque<float>& samples = history(selected->name);
    if (!samples.empty()) {
        const auto [lowest, highest] = std::minmax_element(samples.begin(), samples.end());
        low = *lowest;
        high = *highest;
        if (high - low < kMinSpan) high = low + 1.0f;
    }
    ImGui::Text("%s  history", selected->name.c_str());
    plotHistory("##history", samples, low, high, ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));
    const ParamCurve* curve = bus.curve(selected->name);
    if (curve && !curve->points.empty()) drawParamCurve(bus, *selected, *curve);
}

void DebugOverlay::drawObservation(Engine& engine) {
    if (!m_observation) {
        ImGui::TextDisabled("no observation system attached");
        return;
    }
    World& world = engine.world();
    const ObservationStats& stats = m_observation->stats();
    ImGui::Text("source %s  samples %llu  rejected %llu", stats.usingGpu ? "gpu" : "cpu", static_cast<unsigned long long>(stats.gpuSamples), static_cast<unsigned long long>(stats.gpuRejected));
    ImGui::Text("latency last %.1f ms  max %.1f ms  mean %.1f ms  (%u frames)", stats.lastLatencySeconds * 1000.0, stats.maxLatencySeconds * 1000.0, stats.meanLatencySeconds * 1000.0, stats.lastLatencyFrames);
    const EntityHandle focus = m_observation->focusEntity();
    ImGui::Text("focus: %s", world.isAlive(focus) ? world.getName(focus).c_str() : "-");
    for (ObservationState state : {ObservationState::Attended, ObservationState::Seen, ObservationState::Unobserved}) {
        const std::vector<EntityHandle> entities = m_observation->inState(state);
        ImGui::PushStyleColor(ImGuiCol_Text, stateColor(state));
        const bool open = ImGui::TreeNodeEx(stateName(state), state == ObservationState::Unobserved ? 0 : ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu)", stateName(state), entities.size());
        ImGui::PopStyleColor();
        if (!open) continue;
        const size_t listed = std::min(entities.size(), kMaxListedEntities);
        for (size_t entityIndex = 0; entityIndex < listed; ++entityIndex) {
            const EntityHandle entity = entities[entityIndex];
            ImGui::BulletText("%s", world.isAlive(entity) ? world.getName(entity).c_str() : "<dead>");
        }
        if (entities.size() > kMaxListedEntities) ImGui::TextDisabled("+%zu more", entities.size() - kMaxListedEntities);
        ImGui::TreePop();
    }
}

void DebugOverlay::drawTrace(Engine&) {
    if (!m_trace) {
        ImGui::TextDisabled("no decision trace attached");
        return;
    }
    const auto& events = m_trace->events();
    ImGui::Text("%zu events  %s", events.size(), m_trace->enabled() ? "recording" : "paused");
    const size_t shown = std::min(events.size(), kMaxTraceEvents);
    for (size_t offset = 0; offset < shown; ++offset) {
        const size_t eventIndex = events.size() - 1 - offset;
        ImGui::PushID(static_cast<int>(eventIndex));
        drawTraceEvent(events[eventIndex], offset == 0);
        ImGui::PopID();
    }
}

void DebugOverlay::drawPerf(Engine& engine) {
    const float frameMilliseconds = m_frameMs.empty() ? 0.0f : m_frameMs.back();
    const double gpuTotal = engine.gpuTotalMilliseconds();
    ImGui::Text("cpu frame %.2f ms  gpu %.2f ms  over capacity %zu", frameMilliseconds, gpuTotal, engine.renderOverCapacity());
    ImGui::Text("entities %zu  tick %llu", engine.world().aliveCount(), static_cast<unsigned long long>(engine.time().frame));
    plotHistory("##cpu", m_frameMs, 0.0f, kFrameGraphMaxMs, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));
    plotHistory("##gpu", m_gpuMs, 0.0f, kFrameGraphMaxMs, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));
    drawGpuPassTable(engine.gpuTimings(), gpuTotal);
    const auto& views = engine.portalStats();
    if (views.empty() || !ImGui::CollapsingHeader("Per-view GPU", ImGuiTreeNodeFlags_DefaultOpen)) return;
    for (const PortalViewStats& view : views) ImGui::Text("%*s%s  %u drawn  %.3f ms", view.depth * 2, "", view.label.c_str(), view.drawn, view.gpuMilliseconds);
}
