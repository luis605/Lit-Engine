module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

module Engine.Profiler;

Profiler::Profiler(size_t historyFrames) : m_capacity(std::max<size_t>(historyFrames, 1)), m_origin(std::chrono::steady_clock::now()) {}

double Profiler::nowMs() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_origin).count(); }

void Profiler::beginFrame() {
    if (!m_enabled) return;
    if (!m_current.empty() || m_frame > 0) endFrame();
    m_frameStartMs = nowMs();
    m_depth = 0;
}

void Profiler::endFrame() {
    if (!m_enabled) return;
    m_lastFrameMs = nowMs() - m_frameStartMs;
    m_previous = m_current;
    m_history.push_back(std::move(m_current));
    m_current.clear();
    while (m_history.size() > m_capacity) m_history.pop_front();
    ++m_frame;
}

int64_t Profiler::begin(std::string_view name) {
    if (!m_enabled) return -1;
    ProfileRecord record;
    record.name = std::string(name);
    record.depth = m_depth++;
    record.startMs = nowMs();
    record.frame = m_frame;
    m_current.push_back(std::move(record));
    return static_cast<int64_t>(m_current.size() - 1);
}

void Profiler::end(int64_t token) {
    if (token < 0) return;
    if (m_depth > 0) --m_depth;
    if (static_cast<size_t>(token) >= m_current.size()) return;
    m_current[static_cast<size_t>(token)].durationMs = nowMs() - m_current[static_cast<size_t>(token)].startMs;
}

std::vector<ProfileSummary> Profiler::topScopes(size_t count) const {
    std::unordered_map<std::string, ProfileSummary> merged;
    for (const ProfileRecord& r : m_previous) {
        ProfileSummary& s = merged[r.name];
        s.name = r.name;
        s.totalMs += r.durationMs;
        ++s.calls;
    }
    std::vector<ProfileSummary> out;
    out.reserve(merged.size());
    for (auto& [name, summary] : merged) out.push_back(std::move(summary));
    std::sort(out.begin(), out.end(), [](const ProfileSummary& a, const ProfileSummary& b) { return a.totalMs != b.totalMs ? a.totalMs > b.totalMs : a.name < b.name; });
    if (out.size() > count) out.resize(count);
    return out;
}

void Profiler::writeChromeTrace(std::ostream& out) const {
    out << "{\"traceEvents\":[";
    bool first = true;
    const auto emit = [&](const ProfileRecord& r) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":\"";
        for (char c : r.name) {
            if (c == '"' || c == '\\') out << '\\';
            out << c;
        }
        out << "\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":" << std::fixed << std::setprecision(3) << r.startMs * 1000.0 << ",\"dur\":" << r.durationMs * 1000.0 << ",\"args\":{\"frame\":" << r.frame << ",\"depth\":" << r.depth << "}}";
    };
    for (const auto& frame : m_history) {
        for (const ProfileRecord& r : frame) emit(r);
    }
    for (const ProfileRecord& r : m_current) emit(r);
    out << "]}";
}

bool Profiler::writeChromeTrace(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    writeChromeTrace(out);
    return static_cast<bool>(out);
}

void Profiler::clear() {
    m_current.clear();
    m_previous.clear();
    m_history.clear();
    m_frame = 0;
    m_depth = 0;
}
