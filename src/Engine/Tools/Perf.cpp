module;

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

module Engine.Perf;

namespace {

std::string columnName(const std::string& name) {
    std::string out = "gpu:";
    for (const char c : name) out += (c == ',' || c == '"' || c == '\n') ? '_' : c;
    return out;
}

}  // namespace

void PerfLog::open(std::string path) {
    close();
    m_path = std::move(path);
    m_columns.clear();
    m_rows.clear();
}

void PerfLog::record(uint64_t frame, double cpuMs, double simMs, double gpuMs, const std::vector<PerfPass>& passes) {
    if (m_path.empty()) return;
    Row row;
    row.frame = frame;
    row.cpu = cpuMs;
    row.sim = simMs;
    row.gpu = gpuMs;
    for (const PerfPass& pass : passes) {
        const std::string name = columnName(pass.name);
        const auto it = std::find(m_columns.begin(), m_columns.end(), name);
        size_t index = static_cast<size_t>(it - m_columns.begin());
        if (it == m_columns.end()) m_columns.push_back(name);
        row.passes.emplace_back(index, pass.milliseconds);
    }
    m_rows.push_back(std::move(row));
}

bool PerfLog::flush() {
    if (m_path.empty()) return false;
    std::ofstream out(m_path, std::ios::trunc);
    if (!out) return false;
    out << "frame,cpu_ms,sim_ms,gpu_ms";
    for (const std::string& c : m_columns) out << ',' << c;
    out << '\n';
    std::vector<double> values(m_columns.size());
    char buf[64];
    for (const Row& row : m_rows) {
        std::fill(values.begin(), values.end(), 0.0);
        for (const auto& [index, ms] : row.passes) values[index] = ms;
        std::snprintf(buf, sizeof buf, "%llu,%.4f,%.4f,%.4f", static_cast<unsigned long long>(row.frame), row.cpu, row.sim, row.gpu);
        out << buf;
        for (const double v : values) {
            std::snprintf(buf, sizeof buf, ",%.4f", v);
            out << buf;
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
