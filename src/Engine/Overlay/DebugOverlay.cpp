module;

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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

constexpr size_t kHistory = 240;

void push(std::deque<float>& values, float value) {
    values.push_back(value);
    while (values.size() > kHistory) values.pop_front();
}

void plot(const char* label, const std::deque<float>& values, float low, float high, ImVec2 size) {
    if (values.empty()) return;
    std::vector<float> flat(values.begin(), values.end());
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

}  // namespace

const std::deque<float>& DebugOverlay::history(const std::string& param) const {
    static const std::deque<float> empty;
    const auto it = m_paramHistory.find(param);
    return it == m_paramHistory.end() ? empty : it->second;
}

void DebugOverlay::sample(Engine& engine) {
    const auto now = std::chrono::steady_clock::now();
    if (m_sampled) push(m_frameMs, std::chrono::duration<float, std::milli>(now - m_lastSample).count());
    m_lastSample = now;
    m_sampled = true;
    push(m_gpuMs, static_cast<float>(engine.gpuTotalMilliseconds()));
    for (const ParamInfo& p : engine.params().params()) push(m_paramHistory[p.name], p.value);
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
        const char* names[] = {"Portals", "Params", "Observation", "AI", "Perf"};
        for (int i = 0; i < 5; ++i) {
            const bool jump = m_requestedTab == i;
            if (ImGui::BeginTabItem(names[i], nullptr, jump ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (i == 0) drawPortals(engine);
                else if (i == 1) drawParams(engine);
                else if (i == 2) drawObservation(engine);
                else if (i == 3) drawTrace(engine);
                else drawPerf(engine);
                ImGui::EndTabItem();
            }
        }
        m_requestedTab = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void DebugOverlay::drawPortals(Engine& engine) {
    PortalSystem& portals = engine.portals();
    const auto& cells = portals.cells();
    const auto& list = portals.portals();
    ImGui::Text("%zu cells  %zu portals  camera cell %s", cells.size(), list.size(), portals.cameraCell() >= 0 ? cells[static_cast<size_t>(portals.cameraCell())].name.c_str() : "-");
    PortalSettings& settings = portals.settings();
    ImGui::SliderInt("Max depth", &settings.maxDepth, 0, 8);
    ImGui::SliderInt("Max views", &settings.maxViews, 1, 64);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 200.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(18, 20, 26, 255));
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const float radius = std::min(size.x, size.y) * 0.5f - 24.0f;
    std::vector<ImVec2> nodes(cells.size());
    for (size_t i = 0; i < cells.size(); ++i) {
        const float a = cells.size() == 1 ? 0.0f : 6.2831853f * static_cast<float>(i) / static_cast<float>(cells.size());
        nodes[i] = ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius);
    }
    for (size_t i = 0; i < list.size(); ++i) {
        const Portal& p = list[i];
        if (p.cell < 0 || p.partner < 0) continue;
        const Portal& q = list[static_cast<size_t>(p.partner)];
        if (q.cell < 0) continue;
        const ImVec2 a = nodes[static_cast<size_t>(p.cell)];
        const ImVec2 b = nodes[static_cast<size_t>(q.cell)];
        if (static_cast<int>(i) < p.partner) draw->AddLine(a, b, IM_COL32(90, 140, 210, 200), 1.5f);
    }
    for (size_t i = 0; i < cells.size(); ++i) {
        const bool here = static_cast<int>(i) == portals.cameraCell();
        draw->AddCircleFilled(nodes[i], here ? 9.0f : 6.0f, here ? IM_COL32(255, 210, 90, 255) : IM_COL32(150, 190, 240, 255));
        draw->AddText(ImVec2(nodes[i].x + 10.0f, nodes[i].y - 7.0f), IM_COL32(220, 224, 232, 255), cells[i].name.c_str());
    }
    ImGui::Dummy(size);
    if (ImGui::CollapsingHeader("Portals", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < list.size(); ++i) {
            const Portal& p = list[i];
            const std::string from = p.cell >= 0 ? cells[static_cast<size_t>(p.cell)].name : "-";
            const std::string to = p.partner >= 0 && list[static_cast<size_t>(p.partner)].cell >= 0 ? cells[static_cast<size_t>(list[static_cast<size_t>(p.partner)].cell)].name : "-";
            ImGui::BulletText("%s  %s -> %s", p.name.c_str(), from.c_str(), to.c_str());
        }
    }
    if (ImGui::CollapsingHeader("Views", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& stats = engine.portalStats();
        if (stats.empty()) ImGui::TextDisabled("no portal views this frame");
        for (const PortalViewStats& v : stats) ImGui::Text("%*s%s  drawn %u  %.3f ms", v.depth * 2, "", v.label.c_str(), v.drawn, v.gpuMilliseconds);
    }
}

void DebugOverlay::drawParams(Engine& engine) {
    ParamBus& bus = engine.params();
    const auto& params = bus.params();
    ImGui::Text("%zu params  file %s  reloads %u", params.size(), bus.watchedPath().empty() ? "-" : bus.watchedPath().c_str(), bus.reloadCount());
    const auto files = engine.watchedFiles();
    ImGui::Text("watched data files %zu  data reloads %u", files.size(), engine.dataReloads());
    if (ImGui::BeginTable("params", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 150.0f))) {
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("value");
        ImGui::TableSetupColumn("target");
        ImGui::TableSetupColumn("slot");
        ImGui::TableSetupColumn("src");
        ImGui::TableHeadersRow();
        for (const ParamInfo& p : params) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(p.name.c_str(), m_selectedParam == p.name, ImGuiSelectableFlags_SpanAllColumns)) m_selectedParam = p.name;
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", p.value);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", p.target);
            ImGui::TableNextColumn();
            ImGui::Text("%u", p.slot);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.derived ? p.input.c_str() : "set");
        }
        ImGui::EndTable();
    }
    const ParamInfo* selected = nullptr;
    for (const ParamInfo& p : params) {
        if (p.name == m_selectedParam) selected = &p;
    }
    if (!selected && !params.empty()) {
        selected = &params.front();
        m_selectedParam = selected->name;
    }
    if (!selected) return;
    float lo = 0.0f, hi = 1.0f;
    const std::deque<float>& hist = history(selected->name);
    if (!hist.empty()) {
        lo = *std::min_element(hist.begin(), hist.end());
        hi = *std::max_element(hist.begin(), hist.end());
        if (hi - lo < 1e-4f) hi = lo + 1.0f;
    }
    ImGui::Text("%s  history", selected->name.c_str());
    plot("##history", hist, lo, hi, ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));
    const ParamCurve* curve = bus.curve(selected->name);
    if (curve && !curve->points.empty()) {
        ImGui::Text("curve %s -> %s", selected->input.c_str(), selected->name.c_str());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, 110.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(18, 20, 26, 255));
        const float x0 = curve->points.front().first;
        const float x1 = curve->points.back().first;
        const float span = std::max(x1 - x0, 1e-4f);
        float ymin = curve->points.front().second, ymax = ymin;
        for (const auto& [x, y] : curve->points) {
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
        const float ys = std::max(ymax - ymin, 1e-4f);
        auto map = [&](float x, float y) { return ImVec2(origin.x + 6.0f + (x - x0) / span * (size.x - 12.0f), origin.y + size.y - 6.0f - (y - ymin) / ys * (size.y - 12.0f)); };
        ImVec2 prev = map(x0, curve->eval(x0));
        for (int i = 1; i <= 96; ++i) {
            const float x = x0 + span * static_cast<float>(i) / 96.0f;
            const ImVec2 next = map(x, curve->eval(x));
            draw->AddLine(prev, next, IM_COL32(120, 190, 255, 255), 2.0f);
            prev = next;
        }
        for (const auto& [x, y] : curve->points) draw->AddCircleFilled(map(x, y), 3.5f, IM_COL32(255, 255, 255, 255));
        if (bus.has(selected->input)) {
            const float in = std::clamp(bus.get(selected->input), x0, x1);
            const ImVec2 live = map(in, curve->eval(in));
            draw->AddLine(ImVec2(live.x, origin.y), ImVec2(live.x, origin.y + size.y), IM_COL32(255, 200, 80, 140));
            draw->AddCircleFilled(live, 5.0f, IM_COL32(255, 200, 80, 255));
        }
        ImGui::Dummy(size);
    }
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
        const std::vector<EntityHandle> list = m_observation->inState(state);
        ImGui::PushStyleColor(ImGuiCol_Text, stateColor(state));
        const bool open = ImGui::TreeNodeEx(stateName(state), state == ObservationState::Unobserved ? 0 : ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu)", stateName(state), list.size());
        ImGui::PopStyleColor();
        if (open) {
            for (size_t i = 0; i < list.size() && i < 64; ++i) ImGui::BulletText("%s", world.isAlive(list[i]) ? world.getName(list[i]).c_str() : "<dead>");
            if (list.size() > 64) ImGui::TextDisabled("+%zu more", list.size() - 64);
            ImGui::TreePop();
        }
    }
}

void DebugOverlay::drawTrace(Engine&) {
    if (!m_trace) {
        ImGui::TextDisabled("no decision trace attached");
        return;
    }
    const auto& events = m_trace->events();
    ImGui::Text("%zu events  %s", events.size(), m_trace->enabled() ? "recording" : "paused");
    const size_t shown = std::min<size_t>(events.size(), 40);
    for (size_t k = 0; k < shown; ++k) {
        const TraceEvent& e = events[events.size() - 1 - k];
        ImGui::PushID(static_cast<int>(events.size() - 1 - k));
        const bool open = ImGui::TreeNodeEx("event", k == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0, "t%llu a%u %s [%s] -> %s", static_cast<unsigned long long>(e.tick), e.agent, e.node.c_str(), e.kind.c_str(), e.winner.c_str());
        if (open) {
            if (!e.reason.empty()) ImGui::TextDisabled("%s", e.reason.c_str());
            for (const TraceOption& o : e.options) {
                const bool win = o.name == e.winner;
                if (win) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 220, 140, 255));
                ImGui::Text("%s%s  %.3f%s", win ? "> " : "  ", o.name.c_str(), o.score, o.available ? "" : "  (unavailable)");
                if (win) ImGui::PopStyleColor();
                for (const TraceConsideration& c : o.considerations) ImGui::Text("      %s  value %.3f  score %.3f", c.input.c_str(), c.value, c.score);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void DebugOverlay::drawPerf(Engine& engine) {
    const float frame = m_frameMs.empty() ? 0.0f : m_frameMs.back();
    ImGui::Text("cpu frame %.2f ms  gpu %.2f ms  over capacity %zu", frame, engine.gpuTotalMilliseconds(), engine.renderOverCapacity());
    ImGui::Text("entities %zu  tick %llu", engine.world().aliveCount(), static_cast<unsigned long long>(engine.time().frame));
    plot("##cpu", m_frameMs, 0.0f, 33.0f, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));
    plot("##gpu", m_gpuMs, 0.0f, 33.0f, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));
    const auto timings = engine.gpuTimings();
    if (ImGui::BeginTable("gpu", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("pass");
        ImGui::TableSetupColumn("ms");
        ImGui::TableSetupColumn("share");
        ImGui::TableHeadersRow();
        const double total = std::max(engine.gpuTotalMilliseconds(), 1e-6);
        for (const GpuPassTiming& t : timings) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", t.milliseconds);
            ImGui::TableNextColumn();
            ImGui::ProgressBar(static_cast<float>(t.milliseconds / total), ImVec2(-1.0f, 0.0f), "");
        }
        ImGui::EndTable();
    }
    const auto& views = engine.portalStats();
    if (!views.empty() && ImGui::CollapsingHeader("Per-view GPU", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const PortalViewStats& v : views) ImGui::Text("%*s%s  %u drawn  %.3f ms", v.depth * 2, "", v.label.c_str(), v.drawn, v.gpuMilliseconds);
    }
}
