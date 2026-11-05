#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bench {

inline constexpr uint64_t kSeed = 0x5eed1234abcdULL;

struct Case {
    double itemsPerIteration = 1.0;
    int iterations = 20;
    int warmup = 3;
    std::function<void()> prepare;
    std::function<void()> run;
};

using Factory = std::function<Case()>;

struct Registration {
    std::string name;
    Factory build;
};

struct Result {
    std::string name;
    int iterations = 0;
    double itemsPerIteration = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double minMs = 0.0;
    double meanMs = 0.0;
    double itemsPerSecond = 0.0;
    double allocsPerIteration = 0.0;
    double bytesPerIteration = 0.0;
};

std::vector<Registration>& registry();
void add(std::string name, Factory build);

Result measure(const std::string& name, const Case& benchCase);

uint64_t allocationCount();
uint64_t allocatedBytes();

bool writeJson(const std::string& path, const std::vector<Result>& results);
int compareWithBaseline(const std::string& path, const std::vector<Result>& results, double tolerancePercent);
void printTable(const std::vector<Result>& results);

void registerWorldBenches();
void registerSpatialBenches();
void registerNavBenches();
void registerAudioBenches();
void registerAiBenches();
void registerAnimationBenches();
void registerPortalBenches();
void registerStreamingBenches();
void registerDataBenches();
void registerAssetBenches();

inline void sink(double value) {
    static volatile double holder = 0.0;
    holder = holder + value;
}

}
