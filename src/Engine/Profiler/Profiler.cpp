module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <ios>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

module Engine.Profiler;

namespace {
constexpr int kTracePrecision = 3;
constexpr double kMicrosecondsPerMillisecond = 1000.0;
constexpr unsigned char kFirstPrintable = 0x20;

void writeEscapedName(std::ostream& out, const std::string& name) {
    for (const char character : name) {
        if (character == '"' || character == '\\') {
            out << '\\' << character;
        } else if (static_cast<unsigned char>(character) < kFirstPrintable) {
            out << ' ';
        } else {
            out << character;
        }
    }
}
}

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
    m_history.push_back(m_current);
    m_previous = std::move(m_current);
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
    for (const ProfileRecord& record : m_previous) {
        ProfileSummary& summary = merged[record.name];
        if (summary.calls == 0) summary.name = record.name;
        summary.totalMs += record.durationMs;
        ++summary.calls;
    }
    std::vector<ProfileSummary> out;
    out.reserve(merged.size());
    for (auto& [name, summary] : merged) out.push_back(std::move(summary));
    const auto byTotalDescending = [](const ProfileSummary& lhs, const ProfileSummary& rhs) { return lhs.totalMs != rhs.totalMs ? lhs.totalMs > rhs.totalMs : lhs.name < rhs.name; };
    const size_t keep = std::min(count, out.size());
    std::partial_sort(out.begin(), out.begin() + keep, out.end(), byTotalDescending);
    out.resize(keep);
    return out;
}

void Profiler::writeChromeTrace(std::ostream& out) const {
    out << "{\"traceEvents\":[";
    const std::ios_base::fmtflags savedFlags = out.flags();
    const std::streamsize savedPrecision = out.precision();
    bool first = true;
    const auto emit = [&](const ProfileRecord& record) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":\"";
        writeEscapedName(out, record.name);
        out << "\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":" << std::fixed << std::setprecision(kTracePrecision) << record.startMs * kMicrosecondsPerMillisecond << ",\"dur\":" << record.durationMs * kMicrosecondsPerMillisecond << ",\"args\":{\"frame\":" << record.frame << ",\"depth\":" << record.depth << "}}";
    };
    for (const auto& frame : m_history) {
        for (const ProfileRecord& record : frame) emit(record);
    }
    for (const ProfileRecord& record : m_current) emit(record);
    out << "]}";
    out.flags(savedFlags);
    out.precision(savedPrecision);
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
