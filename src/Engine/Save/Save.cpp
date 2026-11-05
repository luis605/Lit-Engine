module;

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

module Engine.Save;
import Engine.Json;

import Engine.Rng;
import Engine.camera;
import Engine.glm;
import Engine.Render.entity;

namespace {
constexpr uint32_t kMaxSlotCount = 1u << 24;
constexpr double kMaxSafeCounter = 9.0e15;

uint64_t safeCounter(double value) { return value >= 0.0 && value <= kMaxSafeCounter ? static_cast<uint64_t>(value) : 0; }

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

std::string toHex(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

bool fromHex(const std::string& text, uint64_t& out) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out, 16);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

void syncFile(const std::string& path) {
#if defined(__unix__) || defined(__APPLE__)
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor >= 0) {
        ::fsync(descriptor);
        ::close(descriptor);
    }
#else
    (void)path;
#endif
}

Json number3(double first, double second, double third) {
    Json array = Json::array();
    array.push(first);
    array.push(second);
    array.push(third);
    return array;
}

std::string readWholeFile(const std::string& path, bool& opened) {
    std::optional<std::string> text = readTextFile(path);
    opened = text.has_value();
    return text ? std::move(*text) : std::string();
}

Json captureTime(const TimeState& time) {
    Json json = Json::object();
    json.set("elapsed", time.elapsed);
    json.set("unscaledElapsed", time.unscaledElapsed);
    json.set("frame", time.frame);
    json.set("timeScale", time.timeScale);
    json.set("paused", time.paused);
    return json;
}

Json captureCamera(const Camera& camera) {
    Json json = Json::object();
    const glm::vec3 position = camera.getPosition();
    json.set("position", number3(position.x, position.y, position.z));
    json.set("yaw", camera.getYaw());
    json.set("pitch", camera.getPitch());
    return json;
}

Json captureRandom(World& world) {
    Json random = Json::object();
    random.set("seed", toHex(world.random().seed()));
    Json streams = Json::object();
    for (const auto& [name, rng] : world.random().streams()) {
        Json stream = Json::object();
        stream.set("state", toHex(rng.state()));
        stream.set("increment", toHex(rng.increment()));
        streams.set(name, std::move(stream));
    }
    random.set("streams", std::move(streams));
    return random;
}

Json captureParams(const ParamBus& bus) {
    Json params = Json::object();
    for (const ParamInfo& info : bus.params()) {
        if (info.derived) continue;
        Json entry = Json::object();
        entry.set("value", info.value);
        entry.set("target", info.target);
        params.set(info.name, std::move(entry));
    }
    return params;
}

void applyTime(TimeState& time, const Json& json) {
    if (const Json* value = json.get("elapsed")) time.elapsed = value->asNumber();
    if (const Json* value = json.get("unscaledElapsed")) time.unscaledElapsed = value->asNumber();
    if (const Json* value = json.get("frame")) time.frame = safeCounter(value->asNumber());
    if (const Json* value = json.get("timeScale")) time.timeScale = static_cast<float>(value->asNumber(1.0));
    if (const Json* value = json.get("paused")) time.paused = value->asBool();
}

void applyCamera(Camera& camera, const Json& json) {
    const Json* position = json.get("position");
    if (position && position->items().size() == 3) {
        camera.setPos({static_cast<float>(position->at(0)->asNumber()), static_cast<float>(position->at(1)->asNumber()), static_cast<float>(position->at(2)->asNumber())});
    }
    const Json* yaw = json.get("yaw");
    const Json* pitch = json.get("pitch");
    if (yaw && pitch) camera.setOrientation(static_cast<float>(yaw->asNumber()), static_cast<float>(pitch->asNumber()));
}

bool applyRandom(World& world, const Json& json, std::string* error) {
    uint64_t seed = 0;
    const Json* seedJson = json.get("seed");
    if (!seedJson || !fromHex(seedJson->asString(), seed)) return fail(error, "save: bad random seed");
    world.random().clear();
    world.random().seed(seed);
    const Json* streams = json.get("streams");
    if (!streams) return true;
    for (const auto& [name, entry] : streams->members()) {
        uint64_t state = 0;
        uint64_t increment = 0;
        const Json* stateJson = entry.get("state");
        const Json* incrementJson = entry.get("increment");
        if (!stateJson || !incrementJson || !fromHex(stateJson->asString(), state) || !fromHex(incrementJson->asString(), increment)) {
            return fail(error, "save: bad random stream " + name);
        }
        world.random().stream(name).restore(state, increment);
    }
    return true;
}

bool applyParams(ParamBus& bus, const Json& json, std::string* error) {
    for (const auto& [name, entry] : json.members()) {
        const Json* value = entry.get("value");
        const Json* target = entry.get("target");
        if (!value || !target) return fail(error, "save: bad param " + name);
        const float current = static_cast<float>(value->asNumber());
        if (!bus.has(name)) bus.declare(name, current);
        bus.snap(name, current);
        bus.set(name, static_cast<float>(target->asNumber()));
    }
    return true;
}
}

Json SaveSystem::capture(const std::string& label) const {
    Json body = Json::object();
    Json meta = Json::object();
    meta.set("label", label);
    meta.set("serial", m_serial + 1);
    body.set("meta", std::move(meta));

    if (m_ctx.world) {
        World& world = *m_ctx.world;
        if (m_ctx.deltas) {
            body.set("deltas", m_ctx.deltas->toJson());
        } else {
            body.set("world", world.snapshot());
            uint32_t slots = 0;
            world.forEach([&slots](EntityHandle entity) { slots = std::max(slots, entity.index + 1); });
            body.set("slots", slots);
        }
        body.set("time", captureTime(world.time()));
        body.set("camera", captureCamera(world.camera()));
        body.set("random", captureRandom(world));
    }
    if (m_ctx.state) body.set("state", m_ctx.state->toJson());
    if (m_ctx.storylets) body.set("storylets", m_ctx.storylets->toJson());
    if (m_ctx.params) body.set("params", captureParams(*m_ctx.params));
    if (m_ctx.observation) body.set("observation", m_ctx.observation->toJson());
    Json sections = Json::object();
    for (const auto& [name, section] : m_sections) sections.set(name, section.save());
    body.set("sections", std::move(sections));
    return body;
}

bool SaveSystem::apply(const Json& body, std::string* error) {
    if (!body.isObject()) return fail(error, "save: body is not an object");
    const Json rollback = capture();
    const std::vector<EntityHandle> previousRemap = m_remap;
    const uint64_t previousSerial = m_serial;
    if (applySections(body, error)) return true;
    std::string ignored;
    applySections(rollback, &ignored);
    m_remap = previousRemap;
    m_serial = previousSerial;
    return false;
}

bool SaveSystem::applySections(const Json& body, std::string* error) {
    if (!body.isObject()) return fail(error, "save: body is not an object");
    const Json* sections = body.get("sections");
    if (sections) {
        for (const auto& [name, section] : m_sections) {
            if (sections->get(name) == nullptr) return fail(error, "save: missing section " + name);
        }
    }

    const Json* deltas = body.get("deltas");
    const Json* worldJson = body.get("world");
    if (m_ctx.deltas && deltas && !m_ctx.deltas->fromJson(*deltas, error)) return false;
    if (m_ctx.world && (worldJson || (m_ctx.deltas && deltas)) && !applyWorld(body, error)) return false;
    if (m_ctx.state) {
        const Json* state = body.get("state");
        if (!state) return fail(error, "save: missing state");
        m_ctx.state->reset();
        if (!m_ctx.state->fromJson(*state, error)) return false;
    }
    if (m_ctx.storylets) {
        const Json* story = body.get("storylets");
        if (!story) return fail(error, "save: missing storylets");
        if (!m_ctx.storylets->fromJson(*story, error)) return false;
    }
    if (m_ctx.params) {
        if (const Json* params = body.get("params")) {
            if (!applyParams(*m_ctx.params, *params, error)) return false;
        }
    }
    if (m_ctx.observation) {
        const Json* observation = body.get("observation");
        if (!observation || !m_ctx.observation->fromJson(*observation, [this](EntityHandle handle) { return remap(handle); })) return fail(error, "save: bad observation");
    }
    if (sections) {
        for (const auto& [name, section] : m_sections) {
            const Json* data = sections->get(name);
            if (data && !section.load(*data, error)) return false;
        }
    }
    if (const Json* meta = body.get("meta")) {
        if (const Json* serial = meta->get("serial")) m_serial = safeCounter(serial->asNumber());
    }
    return true;
}

bool SaveSystem::applyWorld(const Json& body, std::string* error) {
    World& world = *m_ctx.world;
    if (const Json* worldJson = body.get("world")) {
        const Json* slots = body.get("slots");
        m_remap.clear();
        const uint64_t requestedSlots = slots ? safeCounter(slots->asNumber()) : 0;
        if (requestedSlots > kMaxSlotCount) return fail(error, "save: slot count out of range");
        const uint32_t slotCount = static_cast<uint32_t>(requestedSlots);
        m_remap.reserve(slotCount);
        for (uint32_t slot = 0; slot < slotCount; ++slot) m_remap.push_back(EntityHandle{slot, 0});
        if (!worldJson->isString() || !world.restore(worldJson->asString(), &m_remap)) return fail(error, "save: world restore failed");
    }
    if (const Json* time = body.get("time")) applyTime(world.timeState(), *time);
    if (const Json* camera = body.get("camera")) applyCamera(world.camera(), *camera);
    if (const Json* random = body.get("random")) return applyRandom(world, *random, error);
    return true;
}

namespace {
bool writeSaveFile(const Json& body, const std::string& path, std::string* error) {
    const std::string bodyText = body.dump();
    std::string text;
    text.reserve(bodyText.size() + 96);
    text += "{\"format\":\"litsave\",\"version\":";
    text += Json(kSaveVersion).dump();
    text += ",\"checksum\":\"";
    text += toHex(hashName(bodyText));
    text += "\",\"body\":";
    text += bodyText;
    text += '}';

    namespace fs = std::filesystem;
    const fs::path target(path);
    std::error_code errorCode;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), errorCode);
    static std::atomic<uint64_t> writeCounter{0};
    const std::string temporaryPath = path + "." + toHex(writeCounter.fetch_add(1) ^ (std::hash<std::thread::id>{}(std::this_thread::get_id()) << 20)) + ".tmp";
    {
        std::ofstream out(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!out) return fail(error, "save: cannot write " + temporaryPath);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            fs::remove(temporaryPath, errorCode);
            return fail(error, "save: write failed " + temporaryPath);
        }
    }
    syncFile(temporaryPath);
    if (fs::exists(target, errorCode)) {
        fs::copy_file(target, path + ".bak", fs::copy_options::overwrite_existing, errorCode);
    }
    fs::rename(temporaryPath, target, errorCode);
    if (errorCode) {
        fs::remove(temporaryPath, errorCode);
        return fail(error, "save: rename failed for " + path);
    }
    return true;
}
}

bool SaveSystem::save(const std::string& path, const std::string& label, std::string* error) {
    waitPending();
    const Json body = capture(label);
    if (!writeSaveFile(body, path, error)) return false;
    ++m_serial;
    return true;
}

bool SaveSystem::saveAsync(const std::string& path, const std::string& label) {
    waitPending();
    Json body = capture(label);
    ++m_serial;
    m_pending = std::async(std::launch::async, [path, snapshot = std::move(body)]() {
        std::string writeError;
        try {
            return writeSaveFile(snapshot, path, &writeError);
        } catch (...) {
            return false;
        }
    });
    return true;
}

bool SaveSystem::waitPending() {
    if (!m_pending.valid()) return true;
    try {
        return m_pending.get();
    } catch (...) {
        return false;
    }
}

bool SaveSystem::pendingWrite() const {
    return m_pending.valid() && m_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

bool SaveSystem::load(const std::string& path, std::string* error) {
    waitPending();
    bool opened = false;
    const std::string text = readWholeFile(path, opened);
    if (!opened) return fail(error, "save: cannot open " + path);
    std::string parseError;
    auto document = Json::parse(text, &parseError);
    if (!document || !document->isObject()) return fail(error, "save: corrupt file (" + parseError + ")");
    const Json* format = document->get("format");
    const Json* version = document->get("version");
    const Json* checksum = document->get("checksum");
    const Json* body = document->get("body");
    if (!format || format->asString() != "litsave" || !version || !version->isNumber() || !checksum || !body) return fail(error, "save: not a save file");
    uint64_t expected = 0;
    if (!fromHex(checksum->asString(), expected) || expected != hashName(body->dump())) return fail(error, "save: checksum mismatch");
    const double rawVersion = version->asNumber();
    if (!(rawVersion >= 1.0 && rawVersion < kSaveVersion + 1.0)) return fail(error, "save: unsupported version");
    int currentVersion = static_cast<int>(rawVersion);
    Json working = *body;
    for (; currentVersion < kSaveVersion; ++currentVersion) {
        const auto migration = m_migrations.find(currentVersion);
        if (migration == m_migrations.end()) return fail(error, "save: no migration from version " + std::to_string(currentVersion));
        if (!migration->second(working, error)) return false;
    }
    return apply(working, error);
}

void SaveSystem::configureAutosave(std::string directory, float intervalSeconds, uint32_t slots) {
    m_autoDir = std::move(directory);
    m_autoInterval = intervalSeconds;
    m_autoSlots = std::max(1u, slots);
    m_autoElapsed = 0.0f;
}

std::string SaveSystem::autosavePath(uint32_t slot) const {
    return (std::filesystem::path(m_autoDir) / ("autosave_" + std::to_string(slot) + ".litsave")).string();
}

bool SaveSystem::autosaveNow(std::string* error) {
    if (m_autoDir.empty()) return fail(error, "save: autosave not configured");
    return save(autosavePath(static_cast<uint32_t>(m_serial % m_autoSlots)), "autosave", error);
}

bool SaveSystem::autosaveAsync(float deltaSeconds) {
    if (m_autoDir.empty() || m_autoInterval <= 0.0f) return false;
    m_autoElapsed += deltaSeconds;
    if (m_autoElapsed < m_autoInterval || pendingWrite()) return false;
    m_autoElapsed = 0.0f;
    return saveAsync(autosavePath(static_cast<uint32_t>(m_serial % m_autoSlots)), "autosave");
}

bool SaveSystem::autosave(float deltaSeconds, std::string* error) {
    if (m_autoDir.empty() || m_autoInterval <= 0.0f) return false;
    m_autoElapsed += deltaSeconds;
    if (m_autoElapsed < m_autoInterval) return false;
    m_autoElapsed = 0.0f;
    return autosaveNow(error);
}

bool SaveSystem::loadLatestAutosave(std::string* error) {
    waitPending();
    if (m_autoDir.empty()) return fail(error, "save: autosave not configured");
    std::vector<std::pair<uint64_t, std::string>> found;
    for (uint32_t slot = 0; slot < m_autoSlots; ++slot) {
        const std::string path = autosavePath(slot);
        bool opened = false;
        const std::string text = readWholeFile(path, opened);
        if (!opened) continue;
        const auto document = Json::parse(text);
        const Json* body = document ? document->get("body") : nullptr;
        const Json* meta = body ? body->get("meta") : nullptr;
        const Json* serial = meta ? meta->get("serial") : nullptr;
        if (serial) found.emplace_back(safeCounter(serial->asNumber()), path);
    }
    std::sort(found.begin(), found.end(), std::greater<>());
    std::string lastError = "save: no autosave found";
    for (const auto& [serial, path] : found) {
        std::string reason;
        if (load(path, &reason)) return true;
        lastError = reason;
    }
    return fail(error, lastError);
}
