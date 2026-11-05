#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Dump;
import Engine.Replay;
import Engine.WorldHash;
import Engine.TestScenes;

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAILURE_CODE = 1;
constexpr int EXIT_USAGE = 2;
constexpr uint64_t DEFAULT_TICKS = 600;
constexpr uint64_t DEFAULT_SEED = 1;
constexpr int DECIMAL_BASE = 10;

struct Options {
    std::string scene;
    std::string record;
    std::string replay;
    std::string verifyReplay;
    std::string params;
    std::string dump;
    std::string perf;
    std::string testScene;
    std::string metricsPath;
    uint64_t ticks = DEFAULT_TICKS;
    uint64_t seed = DEFAULT_SEED;
    float dt = 0.0f;
    uint64_t hashInterval = 0;
    bool ticksGiven = false;
};

constexpr const char* USAGE_TEXT =
    "usage: Headless [options]\n"
    "\n"
    "Runs the engine without a window for a fixed number of ticks, then prints\n"
    "'ticks <n> entities <n> hash <h>' (hash of the state dump) to stdout.\n"
    "\n"
    "options:\n"
    "  --scene <file>      load a .litscene file before running\n"
    "  --testscene <name>  build a built-in test scene (see --list-scenes); its\n"
    "                      suggested tick count is used unless --ticks is given\n"
    "  --ticks <n>         number of ticks to run (default 600; with --replay and\n"
    "                      no --ticks, run to the end of the recording)\n"
    "  --dt <seconds>      fixed step per tick (default 0 = engine fixed timestep)\n"
    "  --seed <n>          random seed (default 1; overridden by --replay)\n"
    "  --verify-replay <f> play back a replay and compare its recorded state hashes\n"
    "                      (exit 1 on mismatch)\n"
    "  --hash-interval <n> with --record, store a world-state hash every n ticks\n"
    "  --params <file>     load a parameter JSON file\n"
    "  --record <file>     record input to a replay file\n"
    "  --replay <file>     play back a replay file\n"
    "  --dump <file>       write the final state as JSON\n"
    "  --perf <file>       write per-frame timings as CSV\n"
    "  --metrics <file>    write test scene metrics as JSON (needs --testscene)\n"
    "  --list-scenes       print the built-in test scene names and exit\n"
    "  -h, --help          show this help and exit\n"
    "\n"
    "exit codes: 0 success, 1 runtime failure (bad scene, params, replay or\n"
    "output file), 2 usage error.\n";

void printUsage(std::FILE* stream) {
    std::fputs(USAGE_TEXT, stream);
}

std::optional<int> parseArguments(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto nextValue = [&]() -> const char* { return index + 1 < argc ? argv[++index] : ""; };
        if (argument == "--scene") {
            options.scene = nextValue();
        } else if (argument == "--ticks") {
            options.ticks = std::strtoull(nextValue(), nullptr, DECIMAL_BASE);
            options.ticksGiven = true;
        } else if (argument == "--dt") {
            options.dt = static_cast<float>(std::atof(nextValue()));
        } else if (argument == "--seed") {
            options.seed = std::strtoull(nextValue(), nullptr, DECIMAL_BASE);
        } else if (argument == "--record") {
            options.record = nextValue();
        } else if (argument == "--replay") {
            options.replay = nextValue();
        } else if (argument == "--verify-replay") {
            options.verifyReplay = nextValue();
        } else if (argument == "--hash-interval") {
            options.hashInterval = std::strtoull(nextValue(), nullptr, DECIMAL_BASE);
        } else if (argument == "--params") {
            options.params = nextValue();
        } else if (argument == "--dump") {
            options.dump = nextValue();
        } else if (argument == "--perf") {
            options.perf = nextValue();
        } else if (argument == "--testscene") {
            options.testScene = nextValue();
        } else if (argument == "--metrics") {
            options.metricsPath = nextValue();
        } else if (argument == "--help" || argument == "-h") {
            printUsage(stdout);
            return EXIT_OK;
        } else if (argument == "--list-scenes") {
            for (const std::string& name : testSceneNames()) std::printf("%s\n", name.c_str());
            return EXIT_OK;
        } else {
            printUsage(stderr);
            return EXIT_USAGE;
        }
    }
    return std::nullopt;
}

bool writeMetrics(const std::string& path, const TestSceneMetrics& metrics) {
    std::FILE* file = std::fopen(path.c_str(), "w");
    if (!file) return false;
    std::fprintf(file, "%s\n", metrics.toJson().c_str());
    std::fclose(file);
    return true;
}

bool prepareEngine(Engine& engine, Options& options, TestSceneInfo& sceneInfo, ReplayData& replayData) {
    engine.openHeadless();
    engine.seed(options.seed);
    if (!options.scene.empty() && !engine.world().loadScene(std::filesystem::path(options.scene))) {
        std::fprintf(stderr, "cannot load scene %s\n", options.scene.c_str());
        return false;
    }
    if (!options.params.empty()) {
        std::string error;
        if (!engine.params().loadFile(options.params, &error)) {
            std::fprintf(stderr, "params: %s\n", error.c_str());
            return false;
        }
    }
    if (!options.testScene.empty()) {
        if (!buildTestScene(engine, options.testScene, TestSceneKit{}, sceneInfo)) {
            std::fprintf(stderr, "unknown test scene %s\n", options.testScene.c_str());
            return false;
        }
        applyTestSceneCamera(engine, sceneInfo);
        if (!options.ticksGiven) options.ticks = sceneInfo.suggestedTicks;
    }
    if (!options.replay.empty() && !engine.startReplay(std::move(replayData))) return false;
    if (!options.record.empty()) engine.startRecording(options.record);
    if (!options.perf.empty()) engine.enablePerfLog(options.perf);
    return true;
}

HashSources hashSourcesOf(Engine& engine) {
    HashSources sources;
    sources.state = engine.state();
    sources.storylets = engine.storylets();
    sources.params = &engine.params();
    return sources;
}

bool annotateRecording(const std::string& path, uint64_t interval, const std::vector<ReplayCheckpoint>& checkpoints) {
    ReplayData data;
    if (!loadReplay(path, data)) return false;
    data.hashInterval = interval;
    data.checkpoints = checkpoints;
    std::ofstream out(path, std::ios::binary);
    out << serializeReplay(data);
    return static_cast<bool>(out);
}

int reportVerification(const ReplayVerifier& verifier) {
    std::printf("verify checked %zu of %zu checkpoints\n", verifier.checked(), verifier.expectedCount());
    for (const ReplayMismatch& mismatch : verifier.mismatches()) {
        std::printf("MISMATCH tick %llu expected %s actual %s\n", static_cast<unsigned long long>(mismatch.tick), hashHex(mismatch.expected).c_str(), hashHex(mismatch.actual).c_str());
    }
    const bool complete = verifier.checked() == verifier.expectedCount();
    if (!verifier.ok() || !complete) return EXIT_FAILURE_CODE;
    std::printf("replay verified\n");
    return EXIT_OK;
}

int reportAndFinish(Engine& engine, const Options& options, const TestSceneInfo& sceneInfo) {
    const std::string text = engine.dumpStateText();
    std::printf("ticks %llu entities %zu hash %s\n", static_cast<unsigned long long>(engine.time().frame), engine.world().aliveCount(), hashHex(hashText(text)).c_str());
    if (sceneInfo.metrics) {
        const TestSceneMetrics metrics = sceneInfo.metrics();
        std::printf("scene %s\n", sceneInfo.name.c_str());
        for (const auto& [name, value] : metrics.values) std::printf("metric %s %.6g\n", name.c_str(), value);
        if (!options.metricsPath.empty() && !writeMetrics(options.metricsPath, metrics)) return EXIT_FAILURE_CODE;
    }
    if (!options.dump.empty() && !engine.writeStateDump(options.dump)) return EXIT_FAILURE_CODE;
    return EXIT_OK;
}

}

int main(int argc, char** argv) {
    Options options;
    if (const std::optional<int> earlyExit = parseArguments(argc, argv, options)) return *earlyExit;

    if (!options.verifyReplay.empty()) options.replay = options.verifyReplay;
    ReplayData replayData;
    if (!options.replay.empty()) {
        std::string error;
        if (!loadReplay(options.replay, replayData, &error)) {
            std::fprintf(stderr, "replay: %s\n", error.c_str());
            return EXIT_FAILURE_CODE;
        }
        options.seed = replayData.seed;
    }
    const bool verifying = !options.verifyReplay.empty();
    if (verifying && replayData.checkpoints.empty()) {
        std::fprintf(stderr, "replay %s has no recorded hashes\n", options.replay.c_str());
        return EXIT_FAILURE_CODE;
    }

    ReplayVerifier verifier;
    verifier.begin(replayData);
    const uint64_t replayLength = replayData.ticks.size();
    Engine engine;
    TestSceneInfo sceneInfo;
    if (!prepareEngine(engine, options, sceneInfo, replayData)) return EXIT_FAILURE_CODE;
    std::vector<ReplayCheckpoint> checkpoints;
    const bool recordingHashes = options.hashInterval > 0 && !options.record.empty();
    const auto observe = [&](uint64_t tick) {
        const bool wanted = verifying ? verifier.due(tick) : recordingHashes && tick % options.hashInterval == 0;
        if (!wanted) return;
        const uint64_t hash = hashWorldState(engine.world(), hashSourcesOf(engine));
        if (verifying) verifier.check(tick, hash);
        else checkpoints.push_back({tick, hash});
    };
    const uint64_t runTicks = options.replay.empty() || options.ticksGiven ? options.ticks : replayLength;
    engine.runHeadless(runTicks, options.dt, observe);
    observe(runTicks);
    engine.disablePerfLog();
    if (recordingHashes && !annotateRecording(options.record, options.hashInterval, checkpoints)) return EXIT_FAILURE_CODE;
    const int reportCode = reportAndFinish(engine, options, sceneInfo);
    if (verifying) {
        const int verifyCode = reportVerification(verifier);
        return verifyCode != EXIT_OK ? verifyCode : reportCode;
    }
    return reportCode;
}
