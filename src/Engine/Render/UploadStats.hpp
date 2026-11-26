#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"

extern "C" {
extern std::atomic<unsigned long long> g_LitUploadHeapBytes;
extern std::atomic<unsigned long long> g_LitDynamicHeapBytes;
}

namespace Lit::UploadStats {

struct Counters {
    unsigned long long upload = 0;
    unsigned long long dynamic = 0;
};

inline Counters snapshot() { return {g_LitUploadHeapBytes.load(std::memory_order_relaxed), g_LitDynamicHeapBytes.load(std::memory_order_relaxed)}; }

struct SiteTotals {
    unsigned long long bytes = 0;
    unsigned long long steadyBytes = 0;
    unsigned long long calls = 0;
    unsigned long long frameBytes = 0;
};

class Registry {
public:
    static Registry& get() {
        static Registry registry;
        return registry;
    }
    bool enabled() const { return m_enabled; }
    bool verbose() const { return m_verbose; }

    void attribute(const char* file, int line, const Counters& before) {
        if (!m_enabled) return;
        const Counters after = snapshot();
        const unsigned long long bytes = (after.upload - before.upload) + (after.dynamic - before.dynamic);
        if (bytes == 0) return;
        std::lock_guard lock(m_mutex);
        SiteTotals& site = m_sites[std::string(shorten(file)) + ":" + std::to_string(line)];
        site.bytes += bytes;
        site.frameBytes += bytes;
        ++site.calls;
        if (m_frame >= kWarmupFrames) site.steadyBytes += bytes;
        m_attributedFrame += bytes;
    }

    void endFrame() {
        if (!m_enabled) return;
        std::lock_guard lock(m_mutex);
        const Counters now = snapshot();
        FrameRecord record;
        record.upload = now.upload - m_last.upload;
        record.dynamic = now.dynamic - m_last.dynamic;
        record.attributed = m_attributedFrame;
        m_last = now;
        m_attributedFrame = 0;
        if (m_verbose) {
            std::fprintf(stderr, "[upload] frame %zu: upload-heap %.3f MB, dynamic-heap %.3f MB, attributed %.3f MB\n", m_frames.size(), mb(record.upload), mb(record.dynamic), mb(record.attributed + 0));
        }
        m_frames.push_back(record);
        ++m_frame;
        for (auto& [name, site] : m_sites) site.frameBytes = 0;
    }

    double steadyUploadMB() const { return steadyAverage(true); }
    double steadyDynamicMB() const { return steadyAverage(false); }
    double firstFrameMB() const { return m_frames.empty() ? 0.0 : mb(m_frames.front().upload); }

    std::string report() {
        std::lock_guard lock(m_mutex);
        std::string out;
        char line[512];
        const size_t steadyFrames = m_frames.size() > kWarmupFrames ? m_frames.size() - kWarmupFrames : 0;
        double maxUpload = 0.0;
        for (size_t i = kWarmupFrames; i < m_frames.size(); ++i) maxUpload = std::max(maxUpload, mb(m_frames[i].upload));
        std::snprintf(line, sizeof line, "[upload] %zu frames; frame0 upload-heap %.2f MB; steady (frames >= %zu) upload-heap avg %.3f MB/frame (max %.3f), dynamic-heap avg %.3f MB/frame\n", m_frames.size(), firstFrameMB(), kWarmupFrames, steadyAverage(true), maxUpload, steadyAverage(false));
        out += line;
        std::vector<std::pair<std::string, SiteTotals>> sorted(m_sites.begin(), m_sites.end());
        std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second.steadyBytes > b.second.steadyBytes; });
        out += "[upload] call sites ranked by steady-state MB/frame (total MB over the run, calls):\n";
        double attributedSteady = 0.0;
        for (const auto& [name, site] : sorted) attributedSteady += static_cast<double>(site.steadyBytes);
        for (size_t i = 0; i < sorted.size() && i < 24; ++i) {
            const auto& [name, site] = sorted[i];
            const double perFrame = steadyFrames ? mb(site.steadyBytes) / static_cast<double>(steadyFrames) : 0.0;
            std::snprintf(line, sizeof line, "[upload]   %-44s %9.4f MB/frame %10.2f MB total %8llu calls\n", name.c_str(), perFrame, mb(site.bytes), site.calls);
            out += line;
        }
        std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
        out += "[upload] call sites ranked by total MB over the run (includes scene load):\n";
        for (size_t i = 0; i < sorted.size() && i < 8; ++i) {
            std::snprintf(line, sizeof line, "[upload]   %-44s %10.2f MB total %8llu calls\n", sorted[i].first.c_str(), mb(sorted[i].second.bytes), sorted[i].second.calls);
            out += line;
        }
        const double unattributed = steadyAverage(true) + steadyAverage(false) - (steadyFrames ? mb(static_cast<unsigned long long>(attributedSteady)) / static_cast<double>(steadyFrames) : 0.0);
        std::snprintf(line, sizeof line, "[upload]   unattributed (uninstrumented sites): %.4f MB/frame\n", unattributed);
        out += line;
        unsigned long long attributedTotal = 0;
        for (const auto& [name, site] : sorted) attributedTotal += site.bytes;
        const Counters all = snapshot();
        std::snprintf(line, sizeof line, "[upload]   run total %.2f MB (upload heap %.2f + dynamic %.2f), attributed %.2f MB\n", mb(all.upload + all.dynamic), mb(all.upload), mb(all.dynamic), mb(attributedTotal));
        out += line;
        return out;
    }

    static constexpr size_t kWarmupFrames = 8;

private:
    struct FrameRecord {
        unsigned long long upload = 0;
        unsigned long long dynamic = 0;
        unsigned long long attributed = 0;
    };

    Registry() {
        const char* value = std::getenv("LIT_UPLOAD_STATS");
        m_enabled = value && *value && *value != '0';
        m_verbose = m_enabled && *value == '2';
    }
    ~Registry() {
        if (!m_enabled) return;
        const std::string text = report();
        std::fputs(text.c_str(), stderr);
        if (const char* path = std::getenv("LIT_UPLOAD_STATS_FILE"); path && *path) {
            if (std::FILE* file = std::fopen(path, "w")) {
                std::fputs(text.c_str(), file);
                std::fclose(file);
            }
        }
    }
    static double mb(unsigned long long bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }
    static const char* shorten(const char* path) {
        const char* slash = std::strrchr(path, '/');
        return slash ? slash + 1 : path;
    }
    double steadyAverage(bool upload) const {
        if (m_frames.size() <= kWarmupFrames) return 0.0;
        unsigned long long sum = 0;
        for (size_t i = kWarmupFrames; i < m_frames.size(); ++i) sum += upload ? m_frames[i].upload : m_frames[i].dynamic;
        return mb(sum) / static_cast<double>(m_frames.size() - kWarmupFrames);
    }

    bool m_enabled = false;
    bool m_verbose = false;
    size_t m_frame = 0;
    Counters m_last;
    unsigned long long m_attributedFrame = 0;
    std::vector<FrameRecord> m_frames;
    std::map<std::string, SiteTotals> m_sites;
    std::mutex m_mutex;
};

inline void endFrame() { Registry::get().endFrame(); }

template <class T>
class CallScope {
public:
    CallScope(T* pointer, const char* file, int line) : m_pointer(pointer), m_file(file), m_line(line), m_before(snapshot()) {}
    ~CallScope() { Registry::get().attribute(m_file, m_line, m_before); }
    CallScope(const CallScope&) = delete;
    CallScope& operator=(const CallScope&) = delete;
    T* operator->() const { return m_pointer; }

private:
    T* m_pointer;
    const char* m_file;
    int m_line;
    Counters m_before;
};

template <class T>
T* rawPointer(T* pointer) { return pointer; }
template <class T>
T* rawPointer(const Diligent::RefCntAutoPtr<T>& pointer) { return pointer.RawPtr(); }

}

#define LIT_UP(context) ::Lit::UploadStats::CallScope(::Lit::UploadStats::rawPointer(context), __FILE__, __LINE__)
#define LIT_UP_MARK(name) const ::Lit::UploadStats::Counters name = ::Lit::UploadStats::snapshot()
#define LIT_UP_COMMIT(name) ::Lit::UploadStats::Registry::get().attribute(__FILE__, __LINE__, name)
