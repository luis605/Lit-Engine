// Bent bridge: GPU mode. A law asks for a frame with rxGpuShot({scene, settings, env, ...}); the frame is rendered by LitShot (the
// same Engine rendering path as src/Shot, deterministic clock, fixed window, 320x180 by default) in a child process, which keeps the
// shared GPU's memory bounded (one process at a time, retried on out-of-memory / device loss) and isolates driver crashes. The
// result is a record of frame statistics read from the PNG, the process log and the upload statistics. Images are compared with
// rxImageDiff. Natives are only usable after setGpuMode(true) (`bent --gpu`).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <png.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Engine/Bent/Ext_Gpu.hpp"
#include "Engine/Bent/Interp.hpp"

extern char** environ;

namespace bent {

namespace {

std::atomic<bool> gGpuMode{false};
std::mutex gGpuLock;  // one LitShot at a time (shared GPU) and the cache below
std::map<std::string, Value> gShots;

std::filesystem::path scratchDir() {
    static const std::filesystem::path dir = [] {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("bent-gpu-" + std::to_string(::getpid()));
        std::filesystem::create_directories(path);
        return path;
    }();
    return dir;
}

struct ScratchCleaner {
    ~ScratchCleaner() {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / ("bent-gpu-" + std::to_string(::getpid())), ec);
    }
} gCleaner;

std::filesystem::path exeDir() {
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
}

std::filesystem::path litShotPath() {
    if (const char* env = std::getenv("LITSHOT"); env && *env) return env;
    const std::filesystem::path beside = exeDir() / ".." / "Shot" / "LitShot";
    std::error_code ec;
    if (std::filesystem::exists(beside, ec)) return std::filesystem::weakly_canonical(beside, ec);
    for (const char* candidate : {"build-release/src/Shot/LitShot", "build/src/Shot/LitShot", "src/Shot/LitShot"}) {
        if (std::filesystem::exists(candidate, ec)) return std::filesystem::absolute(candidate, ec);
    }
    return {};
}

std::filesystem::path shotWorkingDir() {
    std::error_code ec;
    if (std::filesystem::exists("resources", ec)) return std::filesystem::current_path();
    const std::filesystem::path build = exeDir() / ".." / "..";
    if (std::filesystem::exists(build / "resources", ec)) return std::filesystem::weakly_canonical(build, ec);
    return std::filesystem::current_path();
}

bool safeToken(const std::string& text) {
    if (text.empty() || text.size() > 200) return false;
    for (const char c : text) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-' || c == ',' || c == '+' || c == '/' || c == ' ')) return false;
    }
    return true;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

int64_t countOf(const std::string& haystack, const std::string& needle) {
    int64_t count = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + needle.size())) ++count;
    return count;
}

struct RunResult {
    bool ok = false;
    int status = -1;
    bool timedOut = false;
};

RunResult spawnWait(const std::vector<std::string>& args, const std::vector<std::pair<std::string, std::string>>& env, const std::filesystem::path& cwd, const std::filesystem::path& log, double timeoutSeconds) {
    std::vector<char*> argv;
    std::vector<std::string> storage = args;
    for (std::string& a : storage) argv.push_back(a.data());
    argv.push_back(nullptr);
    std::vector<std::string> envStorage;
    for (char** e = environ; e && *e; ++e) {
        const std::string entry = *e;
        const std::string key = entry.substr(0, entry.find('='));
        bool replaced = false;
        for (const auto& [k, v] : env) replaced = replaced || k == key;
        if (!replaced) envStorage.push_back(entry);
    }
    for (const auto& [k, v] : env) envStorage.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (std::string& e : envStorage) envp.push_back(e.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    RunResult result;
    if (rc != 0) return result;
    const auto start = std::chrono::steady_clock::now();
    int status = 0;
    for (;;) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > timeoutSeconds) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            result.timedOut = true;
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    result.ok = result.status == 0;
    return result;
}

struct Image {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> rgb;  // 3 bytes per pixel
    std::vector<float> lum;          // Rec. 709 luma of the sRGB bytes, 0..1
};

bool loadImage(const std::string& path, Image& out) {
    png_image png;
    std::memset(&png, 0, sizeof png);
    png.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&png, path.c_str())) return false;
    png.format = PNG_FORMAT_RGB;
    const int w = static_cast<int>(png.width);
    const int h = static_cast<int>(png.height);
    out.w = w;
    out.h = h;
    out.rgb.resize(PNG_IMAGE_SIZE(png));
    if (!png_image_finish_read(&png, nullptr, out.rgb.data(), 0, nullptr)) {
        png_image_free(&png);
        return false;
    }
    out.lum.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.lum.size(); ++i) out.lum[i] = (0.2126f * out.rgb[i * 3] + 0.7152f * out.rgb[i * 3 + 1] + 0.0722f * out.rgb[i * 3 + 2]) / 255.0f;
    return true;
}

uint64_t hashBytes(const std::vector<unsigned char>& bytes) {
    uint64_t h = 14695981039346656037ull;
    for (const unsigned char b : bytes) h = (h ^ b) * 1099511628211ull;
    return h;
}

// Mean luma of 8x8 blocks.
std::vector<float> blockMeans(const Image& img, int block, int& bw, int& bh) {
    bw = img.w / block;
    bh = img.h / block;
    std::vector<float> means(static_cast<size_t>(bw) * bh, 0.0f);
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            double sum = 0.0;
            for (int y = 0; y < block; ++y) {
                for (int x = 0; x < block; ++x) sum += img.lum[static_cast<size_t>(by * block + y) * img.w + bx * block + x];
            }
            means[static_cast<size_t>(by) * bw + bx] = static_cast<float>(sum / (block * block));
        }
    }
    return means;
}

Value statsRecord(const Image& img) {
    double sum = 0.0, sum2 = 0.0, minL = 1.0, maxL = 0.0;
    int64_t black = 0, white = 0, exactBlack = 0;
    for (size_t i = 0; i < img.lum.size(); ++i) {
        const double l = img.lum[i];
        sum += l;
        sum2 += l * l;
        minL = std::min(minL, l);
        maxL = std::max(maxL, l);
        if (l < 0.02) ++black;
        if (l > 0.98) ++white;
        if (img.rgb[i * 3] == 0 && img.rgb[i * 3 + 1] == 0 && img.rgb[i * 3 + 2] == 0) ++exactBlack;
    }
    const double n = static_cast<double>(img.lum.size());
    const double mean = sum / n;
    // Structure: mean absolute luma gradient, and isolated dark blocks (a block much darker than all eight neighbours).
    double gradient = 0.0;
    for (int y = 0; y + 1 < img.h; ++y) {
        for (int x = 0; x + 1 < img.w; ++x) {
            const float l = img.lum[static_cast<size_t>(y) * img.w + x];
            gradient += std::abs(img.lum[static_cast<size_t>(y) * img.w + x + 1] - l) + std::abs(img.lum[static_cast<size_t>(y + 1) * img.w + x] - l);
        }
    }
    int bw = 0, bh = 0;
    const std::vector<float> blocks = blockMeans(img, 8, bw, bh);
    int64_t blobs = 0, interior = 0;
    for (int by = 1; by + 1 < bh; ++by) {
        for (int bx = 1; bx + 1 < bw; ++bx) {
            float lowestNeighbour = 1e9f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx || dy) lowestNeighbour = std::min(lowestNeighbour, blocks[static_cast<size_t>(by + dy) * bw + bx + dx]);
                }
            }
            ++interior;
            if (lowestNeighbour > 0.06f && blocks[static_cast<size_t>(by) * bw + bx] < 0.5f * lowestNeighbour) ++blobs;
        }
    }
    const auto region = [&](double y0, double y1) {
        double s = 0.0;
        int64_t c = 0;
        for (int y = static_cast<int>(y0 * img.h); y < static_cast<int>(y1 * img.h); ++y) {
            for (int x = 0; x < img.w; ++x) {
                s += img.lum[static_cast<size_t>(y) * img.w + x];
                ++c;
            }
        }
        return c ? s / c : 0.0;
    };
    return Value::record({{"w", Value::integer(img.w)},
                          {"h", Value::integer(img.h)},
                          {"mean", Value::number(mean)},
                          {"std", Value::number(std::sqrt(std::max(0.0, sum2 / n - mean * mean)))},
                          {"min", Value::number(minL)},
                          {"max", Value::number(maxL)},
                          {"blackFrac", Value::number(static_cast<double>(black) / n)},
                          {"exactBlackFrac", Value::number(static_cast<double>(exactBlack) / n)},
                          {"whiteFrac", Value::number(static_cast<double>(white) / n)},
                          {"gradient", Value::number(gradient / n)},
                          {"blobScore", Value::number(interior ? static_cast<double>(blobs) / interior : 0.0)},
                          {"top", Value::number(region(0.0, 0.25))},
                          {"bottom", Value::number(region(0.75, 1.0))},
                          {"hash", Value::integer(static_cast<int64_t>(hashBytes(img.rgb) & 0x7fffffffffffffffULL))}});
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

double numberAfter(const std::string& text, const std::string& key, double fallback) {
    const size_t at = text.find(key);
    if (at == std::string::npos) return fallback;
    return std::strtod(text.c_str() + at + key.size(), nullptr);
}

const Value* field(const Value& record, const char* name) { return record.kind == Kind::Record ? record.field(name) : nullptr; }

Value shotRecord(bool ok, const std::string& error) { return Value::record({{"ok", Value::boolean(ok)}, {"error", Value::string(error)}}); }

Value renderShot(const Value& spec, const SrcLoc& loc) {
    if (!gGpuMode) throw BentError(loc, "rxGpuShot: GPU mode is off (run bent with --gpu)");
    const std::string key = print(spec);
    std::lock_guard<std::mutex> guard(gGpuLock);
    if (const auto it = gShots.find(key); it != gShots.end()) return it->second;

    const std::filesystem::path shot = litShotPath();
    if (shot.empty()) return gShots[key] = shotRecord(false, "LitShot not found (build the LitShot target or set LITSHOT)");
    const auto stringField = [&](const char* name, const std::string& fallback) {
        const Value* v = field(spec, name);
        return v && v->kind == Kind::Str ? v->text() : fallback;
    };
    const auto intField = [&](const char* name, int64_t fallback) {
        const Value* v = field(spec, name);
        return v && v->isNumber() ? static_cast<int64_t>(v->num()) : fallback;
    };
    const std::string scene = stringField("scene", "vista");
    if (!safeToken(scene)) throw BentError(loc, "rxGpuShot: bad scene name");
    const int64_t frames = std::clamp<int64_t>(intField("frames", 20), 2, 300);
    const int64_t width = std::clamp<int64_t>(intField("width", 320), 64, 1280);
    const int64_t height = std::clamp<int64_t>(intField("height", 180), 64, 720);

    static std::atomic<uint64_t> counter{0};
    const std::string stem = "shot" + std::to_string(counter.fetch_add(1));
    const std::filesystem::path png = scratchDir() / (stem + ".png");
    const std::filesystem::path log = scratchDir() / (stem + ".log");
    const std::filesystem::path uploads = scratchDir() / (stem + ".uploads");

    std::vector<std::string> args = {shot.string(), "--deterministic", "--testscene", scene, "--frames", std::to_string(frames), "--size", std::to_string(width) + "x" + std::to_string(height), "--screenshot", png.string()};
    if (const Value* settings = field(spec, "settings"); settings && settings->kind == Kind::List) {
        for (const Value& pair : settings->items()) {
            if (pair.kind != Kind::List || pair.items().size() != 2) throw BentError(loc, "rxGpuShot: settings are [key, value] pairs");
            const std::string k = pair.items()[0].kind == Kind::Str ? pair.items()[0].text() : "";
            const Value& v = pair.items()[1];
            const std::string text = v.kind == Kind::Str ? v.text() : (v.kind == Kind::Bool ? (v.b ? "1" : "0") : print(v));
            if (!safeToken(k) || !safeToken(text)) throw BentError(loc, "rxGpuShot: unsafe setting " + k);
            args.push_back("--set");
            args.push_back(k + "=" + text);
        }
    }
    if (const Value* camera = field(spec, "camera"); camera && camera->kind == Kind::List && camera->items().size() >= 5) {
        std::string text;
        for (size_t i = 0; i < camera->items().size() && i < 6; ++i) text += (i ? "," : "") + printFloat(camera->items()[i].num());
        args.push_back("--camera");
        args.push_back(text);
    }
    if (const Value* extra = field(spec, "args"); extra && extra->kind == Kind::List) {
        for (const Value& a : extra->items()) {
            if (a.kind != Kind::Str || !safeToken(a.text())) throw BentError(loc, "rxGpuShot: unsafe extra argument");
            args.push_back(a.text());
        }
    }
    std::vector<std::pair<std::string, std::string>> env = {{"LIT_UPLOAD_STATS", "1"}, {"LIT_UPLOAD_STATS_FILE", uploads.string()}};
    if (const Value* e = field(spec, "env"); e && e->kind == Kind::List) {
        for (const Value& pair : e->items()) {
            if (pair.kind != Kind::List || pair.items().size() != 2) throw BentError(loc, "rxGpuShot: env entries are [name, value]");
            const std::string k = pair.items()[0].text();
            const std::string v = pair.items()[1].kind == Kind::Str ? pair.items()[1].text() : print(pair.items()[1]);
            if (k.rfind("LIT_", 0) != 0 || !safeToken(k) || !safeToken(v)) throw BentError(loc, "rxGpuShot: only LIT_* environment variables are allowed");
            env.emplace_back(k, v);
        }
    }

    // Retry: the GPU is shared, so a render can fail with out-of-memory or device loss through no fault of the engine.
    RunResult run;
    std::string logText;
    bool oom = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::error_code ec;
        std::filesystem::remove(png, ec);
        run = spawnWait(args, env, shotWorkingDir(), log, 120.0);
        logText = readFile(log);
        std::error_code exists;
        oom = lower(logText).find("out_of_device_memory") != std::string::npos;
        if (run.ok && !oom && std::filesystem::exists(png, exists)) break;
        std::this_thread::sleep_for(std::chrono::seconds(2 + 2 * attempt));
    }
    // The GPU is shared: persistent out-of-memory says nothing about the engine, so the case is discarded instead of failed.
    if (oom) throw Discard{"GPU out of memory (shared device)"};
    std::error_code exists;
    if (!run.ok || !std::filesystem::exists(png, exists)) {
        const std::string tail = logText.size() > 400 ? logText.substr(logText.size() - 400) : logText;
        return gShots[key] = shotRecord(false, std::string(run.timedOut ? "timed out" : "exit status " + std::to_string(run.status)) + ": " + tail);
    }
    Image image;
    if (!loadImage(png.string(), image)) return gShots[key] = shotRecord(false, "PNG could not be decoded");

    const std::string lowerLog = lower(logText);
    const int64_t errors = countOf(lowerLog, "[error]") + countOf(lowerLog, "diligent engine: error") + countOf(lowerLog, "vuid") + countOf(lowerLog, "validation error");
    const int64_t warnings = countOf(lowerLog, "diligent engine: warning") + countOf(lowerLog, "[warn");
    const std::string uploadText = readFile(uploads);
    std::vector<std::pair<std::string, Value>> fields = {{"ok", Value::boolean(true)},
                                                          {"error", Value::string("")},
                                                          {"png", Value::string(png.string())},
                                                          {"logErrors", Value::integer(errors)},
                                                          {"logWarnings", Value::integer(warnings)},
                                                          {"bindless", Value::boolean(logText.find("Bindless texture table: on") != std::string::npos)},
                                                          {"uploadMB", Value::number(numberAfter(uploadText, "upload-heap avg ", -1.0))},
                                                          {"dynamicMB", Value::number(numberAfter(uploadText, "dynamic-heap avg ", -1.0))},
                                                          {"firstFrameMB", Value::number(numberAfter(uploadText, "frame0 upload-heap ", -1.0))},
                                                          {"logTail", Value::string(logText.size() > 600 ? logText.substr(logText.size() - 600) : logText)}};
    const Value stats = statsRecord(image);
    for (const auto& [name, value] : stats.rec->fields) fields.emplace_back(name, value);
    return gShots[key] = Value::record(std::move(fields));
}

// rxImageDiff(a, b): per-channel byte differences between two shots, and the difference of their 20x20 block mean lumas (structure).
Value imageDiff(const Value& a, const Value& b, const SrcLoc& loc) {
    const Value* pa = field(a, "png");
    const Value* pb = field(b, "png");
    if (!pa || !pb) throw BentError(loc, "rxImageDiff: both arguments must be successful rxGpuShot results");
    Image ia, ib;
    if (!loadImage(pa->text(), ia) || !loadImage(pb->text(), ib)) throw BentError(loc, "rxImageDiff: cannot load images");
    const bool same = ia.w == ib.w && ia.h == ib.h;
    double meanAbs = 0.0, maxAbs = 0.0;
    int64_t differing = 0;
    if (same) {
        for (size_t i = 0; i < ia.rgb.size(); ++i) {
            const double d = std::abs(static_cast<int>(ia.rgb[i]) - static_cast<int>(ib.rgb[i]));
            meanAbs += d;
            maxAbs = std::max(maxAbs, d);
        }
        for (size_t p = 0; p < ia.lum.size(); ++p) {
            int d = 0;
            for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(static_cast<int>(ia.rgb[p * 3 + c]) - static_cast<int>(ib.rgb[p * 3 + c])));
            if (d > 8) ++differing;
        }
        meanAbs /= static_cast<double>(ia.rgb.size());
    }
    // Block structure on a fixed 16 x 9 grid, independent of the image size.
    const auto grid = [](const Image& img) {
        std::vector<double> cells(16 * 9, 0.0);
        std::vector<int> counts(16 * 9, 0);
        for (int y = 0; y < img.h; ++y) {
            for (int x = 0; x < img.w; ++x) {
                const int cell = std::min(8, y * 9 / img.h) * 16 + std::min(15, x * 16 / img.w);
                cells[cell] += img.lum[static_cast<size_t>(y) * img.w + x];
                ++counts[cell];
            }
        }
        for (size_t i = 0; i < cells.size(); ++i) cells[i] = counts[i] ? cells[i] / counts[i] : 0.0;
        return cells;
    };
    const std::vector<double> ga = grid(ia), gb = grid(ib);
    double blockMax = 0.0, blockMean = 0.0;
    for (size_t i = 0; i < ga.size(); ++i) {
        const double d = std::abs(ga[i] - gb[i]);
        blockMax = std::max(blockMax, d);
        blockMean += d;
    }
    return Value::record({{"sameSize", Value::boolean(same)},
                          {"meanAbs", Value::number(meanAbs)},
                          {"maxAbs", Value::number(maxAbs)},
                          {"diffFrac", Value::number(same ? static_cast<double>(differing) / static_cast<double>(ia.lum.size()) : 1.0)},
                          {"blockMax", Value::number(blockMax)},
                          {"blockMean", Value::number(blockMean / static_cast<double>(ga.size()))}});
}

}  // namespace

void setGpuMode(bool enabled) { gGpuMode = enabled; }
bool gpuMode() { return gGpuMode; }

bool gpuUsable() {
    static const bool usable = [] {
        const bool before = gGpuMode;
        gGpuMode = true;
        Value r;
        try {
            r = renderShot(Value::record({{"scene", Value::string("vista")}, {"frames", Value::integer(3)}, {"width", Value::integer(64)}, {"height", Value::integer(64)}}), SrcLoc{"<gpu probe>", 0, 0});
        } catch (const Discard&) {
            r = shotRecord(false, "out of memory");
        }
        gGpuMode = before;
        const Value* ok = field(r, "ok");
        return ok && ok->kind == Kind::Bool && ok->b;
    }();
    return usable;
}

namespace ext {

void registerExtGpuBuiltins(Program& program) {
    program.native("rxGpuEnabled", [](Interp&, const ValueList&, const NamedArgs&, const SrcLoc&) { return Value::boolean(gGpuMode); });
    program.native("rxGpuShot", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "rxGpuShot(spec)");
        return renderShot(args[0], loc);
    });
    program.native("rxImageDiff", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "rxImageDiff(a, b)");
        return imageDiff(args[0], args[1], loc);
    });
}

}  // namespace ext

}  // namespace bent
