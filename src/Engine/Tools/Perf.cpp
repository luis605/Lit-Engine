module;

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

module Engine.Perf;

namespace {
constexpr size_t kFormatBufferSize = 64;

std::string sanitizeColumn(const std::string& name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (const char character : name) sanitized += (character == ',' || character == '"' || character == '\n' || character == '\r') ? '_' : character;
    return sanitized;
}

std::string columnName(const std::string& name) { return "gpu:" + sanitizeColumn(name); }
}

size_t PerfLog::columnIndex(const std::string& name) {
    const auto found = std::find(m_columns.begin(), m_columns.end(), name);
    if (found != m_columns.end()) return static_cast<size_t>(found - m_columns.begin());
    m_columns.push_back(name);
    return m_columns.size() - 1;
}

void PerfLog::open(std::string path) {
    close();
    m_path = std::move(path);
    m_columns.clear();
    m_rows.clear();
}

void PerfLog::record(uint64_t frame, double cpuMs, double simMs, double gpuMs, const std::vector<PerfPass>& passes, const std::vector<PerfPass>& extras) {
    if (m_path.empty()) return;
    Row row;
    row.frame = frame;
    row.cpuMilliseconds = cpuMs;
    row.simMilliseconds = simMs;
    row.gpuMilliseconds = gpuMs;
    row.passes.reserve(passes.size() + extras.size());
    for (const PerfPass& pass : passes) row.passes.emplace_back(columnIndex(columnName(pass.name)), pass.milliseconds);
    for (const PerfPass& extra : extras) row.passes.emplace_back(columnIndex(sanitizeColumn(extra.name)), extra.milliseconds);
    m_rows.push_back(std::move(row));
}

bool PerfLog::flush() {
    if (m_path.empty()) return false;
    std::ofstream out(m_path, std::ios::trunc);
    if (!out) return false;
    out << "frame,cpu_ms,sim_ms,gpu_ms";
    for (const std::string& column : m_columns) out << ',' << column;
    out << '\n';
    std::vector<double> values(m_columns.size());
    char buffer[kFormatBufferSize];
    for (const Row& row : m_rows) {
        std::fill(values.begin(), values.end(), 0.0);
        for (const auto& [index, milliseconds] : row.passes) values[index] = milliseconds;
        std::snprintf(buffer, sizeof buffer, "%llu,%.4f,%.4f,%.4f", static_cast<unsigned long long>(row.frame), row.cpuMilliseconds, row.simMilliseconds, row.gpuMilliseconds);
        out << buffer;
        for (const double value : values) {
            std::snprintf(buffer, sizeof buffer, ",%.4f", value);
            out << buffer;
        }
        out << '\n';
    }
    return static_cast<bool>(out);
}

void PerfLog::close() {
    if (m_path.empty()) return;
    flush();
    m_path.clear();
    m_rows.clear();
    m_columns.clear();
}
