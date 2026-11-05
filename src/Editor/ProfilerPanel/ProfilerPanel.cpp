module;

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Editor.profilerpanel;

import Editor.context;
import Engine.engine;
import Engine.Profiler;
import Engine.Json;

namespace {

constexpr size_t kHistoryFrames = 240;
constexpr int kJsonIndent = 2;
constexpr Phase kPhases[] = {Phase::PreUpdate, Phase::FixedUpdate, Phase::Update, Phase::PostUpdate};

std::vector<std::pair<std::string, double>> systemCosts(Engine& engine, const std::vector<ProfileRecord>& records) {
    std::unordered_set<std::string> names;
    for (const Phase phase : kPhases) {
        for (const std::string& name : engine.systemNames(phase)) names.insert(name);
    }
    std::unordered_map<std::string, double> totals;
    for (const ProfileRecord& record : records) {
        if (names.contains(record.name)) totals[record.name] += record.durationMs;
    }
    std::vector<std::pair<std::string, double>> costs(totals.begin(), totals.end());
    std::sort(costs.begin(), costs.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    return costs;
}

}

void ProfilerPanel::toggle(Engine& engine) {
    m_open = !m_open;
    if (m_open && !engine.profiler().enabled()) {
        engine.profiler().setEnabled(true);
        m_enabledProfiler = true;
    }
    if (!m_open && m_enabledProfiler) {
        engine.profiler().setEnabled(false);
        m_enabledProfiler = false;
    }
}

void ProfilerPanel::capture(Engine& engine) {
    const auto& history = engine.profiler().history();
    if (history.empty()) return;
    m_snapshot.records = history.back();
    m_snapshot.cpuMs = engine.profiler().lastFrameMs();
    m_snapshot.gpuMs = engine.gpuTotalMilliseconds();
    m_snapshot.frame = engine.profiler().frameIndex();
    m_snapshot.gpuPasses.clear();
    for (const auto& timing : engine.gpuTimings()) m_snapshot.gpuPasses.emplace_back(timing.name, timing.milliseconds);
    m_snapshot.systems = systemCosts(engine, m_snapshot.records);
    if (m_selected >= static_cast<int>(m_snapshot.records.size())) m_selected = -1;
}

void ProfilerPanel::update(Engine& engine, float frameMs) {
    if (!m_open || m_paused) return;
    m_frameHistory.push_back(frameMs);
    while (m_frameHistory.size() > kHistoryFrames) m_frameHistory.pop_front();
    capture(engine);
}

Json ProfilerPanel::toJson() const {
    Json root = Json::object();
    root.set("frame", m_snapshot.frame);
    root.set("cpuMs", m_snapshot.cpuMs);
    root.set("gpuMs", m_snapshot.gpuMs);
    Json scopes = Json::array();
    for (const ProfileRecord& record : m_snapshot.records) {
        Json scope = Json::object();
        scope.set("name", record.name);
        scope.set("depth", record.depth);
        scope.set("startMs", record.startMs);
        scope.set("durationMs", record.durationMs);
        scopes.push(std::move(scope));
    }
    root.set("scopes", std::move(scopes));
    const auto toArray = [](const std::vector<std::pair<std::string, double>>& rows) {
        Json array = Json::array();
        for (const auto& [name, milliseconds] : rows) {
            Json row = Json::object();
            row.set("name", name);
            row.set("ms", milliseconds);
            array.push(std::move(row));
        }
        return array;
    };
    root.set("gpuPasses", toArray(m_snapshot.gpuPasses));
    root.set("systems", toArray(m_snapshot.systems));
    Json history = Json::array();
    for (const float sample : m_frameHistory) history.push(sample);
    root.set("frameHistoryMs", std::move(history));
    return root;
}

bool ProfilerPanel::exportJson(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << toJson().dump(kJsonIndent);
    return static_cast<bool>(out);
}
