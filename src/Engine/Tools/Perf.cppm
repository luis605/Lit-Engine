module;

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module Engine.Perf;

export struct PerfPass {
    std::string name;
    double milliseconds = 0.0;
};

export class PerfLog {
  public:
    PerfLog() = default;
    ~PerfLog() { close(); }
    PerfLog(const PerfLog&) = delete;
    PerfLog& operator=(const PerfLog&) = delete;

    void open(std::string path);
    void record(uint64_t frame, double cpuMs, double simMs, double gpuMs, const std::vector<PerfPass>& passes);
    bool flush();
    void close();
    [[nodiscard]] bool active() const { return !m_path.empty(); }
    [[nodiscard]] size_t rows() const { return m_rows.size(); }
    [[nodiscard]] const std::string& path() const { return m_path; }

  private:
    struct Row {
        uint64_t frame = 0;
        double cpu = 0.0;
        double sim = 0.0;
        double gpu = 0.0;
        std::vector<std::pair<size_t, double>> passes;
    };

    std::string m_path;
    std::vector<std::string> m_columns;
    std::vector<Row> m_rows;
};
