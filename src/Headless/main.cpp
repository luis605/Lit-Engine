#include <filesystem>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

import Engine.engine;
import Engine.World;
import Engine.Dump;
import Engine.Replay;

int main(int argc, char** argv) {
    std::string scene, record, replay, params, dump, perf;
    uint64_t ticks = 600;
    uint64_t seed = 1;
    float dt = 0.0f;
    bool ticksGiven = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--scene") scene = value();
        else if (arg == "--ticks") ticks = std::strtoull(value(), nullptr, 10), ticksGiven = true;
        else if (arg == "--dt") dt = static_cast<float>(std::atof(value()));
        else if (arg == "--seed") seed = std::strtoull(value(), nullptr, 10);
        else if (arg == "--record") record = value();
        else if (arg == "--replay") replay = value();
        else if (arg == "--params") params = value();
        else if (arg == "--dump") dump = value();
        else if (arg == "--perf") perf = value();
        else {
            std::fprintf(stderr, "usage: Headless [--scene f] [--ticks n] [--dt s] [--seed n] [--record f] [--replay f] [--params f] [--dump f] [--perf f]\n");
            return 2;
        }
    }
    ReplayData replayData;
    if (!replay.empty()) {
        std::string error;
        if (!loadReplay(replay, replayData, &error)) {
            std::fprintf(stderr, "replay: %s\n", error.c_str());
            return 1;
        }
        seed = replayData.seed;
    }
    Engine engine;
    engine.openHeadless();
    engine.seed(seed);
    if (!scene.empty() && !engine.world().loadScene(std::filesystem::path(scene))) {
        std::fprintf(stderr, "cannot load scene %s\n", scene.c_str());
        return 1;
    }
    if (!params.empty()) {
        std::string error;
        if (!engine.params().loadFile(params, &error)) {
            std::fprintf(stderr, "params: %s\n", error.c_str());
            return 1;
        }
    }
    if (!replay.empty() && !engine.startReplay(std::move(replayData))) return 1;
    if (!record.empty()) engine.startRecording(record);
    if (!perf.empty()) engine.enablePerfLog(perf);
    engine.runHeadless(replay.empty() || ticksGiven ? ticks : 0, dt);
    engine.disablePerfLog();
    const std::string text = engine.dumpStateText();
    std::printf("ticks %llu entities %zu hash %s\n", static_cast<unsigned long long>(engine.time().frame), engine.world().aliveCount(), hashHex(hashText(text)).c_str());
    if (!dump.empty() && !engine.writeStateDump(dump)) return 1;
    return 0;
}
