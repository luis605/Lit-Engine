#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Harness.hpp"

namespace {

constexpr double kDefaultTolerancePercent = 10.0;

struct Options {
    std::string jsonPath;
    std::string comparePath;
    std::string filter;
    double tolerancePercent = kDefaultTolerancePercent;
    bool list = false;
};

std::vector<std::string> splitFilters(const std::string& text) {
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(',', begin);
        const size_t stop = end == std::string::npos ? text.size() : end;
        if (stop > begin) parts.push_back(text.substr(begin, stop - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return parts;
}

bool selected(const std::vector<std::string>& filters, const std::string& name) {
    if (filters.empty()) return true;
    for (const std::string& filter : filters) {
        if (name.find(filter) != std::string::npos) return true;
    }
    return false;
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--json" && hasValue) options.jsonPath = argv[++index];
        else if (argument == "--compare" && hasValue) options.comparePath = argv[++index];
        else if (argument == "--filter" && hasValue) options.filter = argv[++index];
        else if (argument == "--tolerance" && hasValue) options.tolerancePercent = std::atof(argv[++index]);
        else if (argument == "--list") options.list = true;
        else return false;
    }
    return true;
}

void registerAll() {
    bench::registerAssetBenches();
    bench::registerWorldBenches();
    bench::registerSpatialBenches();
    bench::registerNavBenches();
    bench::registerAudioBenches();
    bench::registerAiBenches();
    bench::registerAnimationBenches();
    bench::registerPortalBenches();
    bench::registerStreamingBenches();
    bench::registerDataBenches();
}

}

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        std::fprintf(stderr, "usage: LitBench [--filter a,b] [--json out.json] [--compare baseline.json] [--tolerance 10%%] [--list]\n");
        return 2;
    }
    registerAll();
    const std::vector<std::string> filters = splitFilters(options.filter);
    if (options.list) {
        for (const auto& entry : bench::registry()) std::printf("%s\n", entry.name.c_str());
        return 0;
    }
    std::vector<bench::Result> results;
    for (const auto& entry : bench::registry()) {
        if (!selected(filters, entry.name)) continue;
        const bench::Case benchCase = entry.build();
        results.push_back(bench::measure(entry.name, benchCase));
        const bench::Result& result = results.back();
        std::fprintf(stderr, "done %-34s median %.4f ms\n", result.name.c_str(), result.medianMs);
    }
    bench::printTable(results);
    if (!options.jsonPath.empty() && !bench::writeJson(options.jsonPath, results)) {
        std::fprintf(stderr, "cannot write %s\n", options.jsonPath.c_str());
        return 2;
    }
    if (options.comparePath.empty()) return 0;
    const int regressions = bench::compareWithBaseline(options.comparePath, results, options.tolerancePercent);
    if (regressions < 0) return 2;
    if (regressions > 0) std::printf("%d regression(s) beyond %.1f%%\n", regressions, options.tolerancePercent);
    return regressions > 0 ? 1 : 0;
}
