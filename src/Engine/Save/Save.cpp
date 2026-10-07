module;

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

module Engine.Save;

import Engine.Rng;
import Engine.camera;
import Engine.glm;
import Engine.Render.entity;

namespace {
bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

std::string toHex(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

bool fromHex(const std::string& s, uint64_t& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out, 16);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

void syncFile(const std::string& path) {
#if defined(__unix__) || defined(__APPLE__)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    (void)path;
#endif
}

Json number3(double a, double b, double c) {
    Json j = Json::array();
    j.push(a);
    j.push(b);
    j.push(c);
    return j;
}
}

Json SaveSystem::capture(const std::string& label) const {
    Json body = Json::object();
    Json meta = Json::object();
    meta.set("label", label);
    meta.set("serial", m_serial + 1);
    body.set("meta", std::move(meta));

    if (m_ctx.world) {
        World& w = *m_ctx.world;
        body.set("world", w.snapshot());
        uint32_t slots = 0;
        w.forEach([&slots](EntityHandle e) { slots = std::max(slots, e.index + 1); });
        body.set("slots", slots);
        const TimeState& t = w.time();
        Json time = Json::object();
        time.set("elapsed", t.elapsed);
        time.set("unscaledElapsed", t.unscaledElapsed);
        time.set("frame", t.frame);
        time.set("timeScale", t.timeScale);
        time.set("paused", t.paused);
        body.set("time", std::move(time));
        const Camera& c = w.camera();
        Json cam = Json::object();
        const glm::vec3 p = c.getPosition();
        cam.set("position", number3(p.x, p.y, p.z));
        cam.set("yaw", c.getYaw());
        cam.set("pitch", c.getPitch());
        body.set("camera", std::move(cam));
        Json random = Json::object();
        random.set("seed", toHex(w.random().seed()));
        Json streams = Json::object();
        for (const auto& [name, rng] : w.random().streams()) {
            Json s = Json::object();
            s.set("state", toHex(rng.state()));
            s.set("increment", toHex(rng.increment()));
            streams.set(name, std::move(s));
        }
        random.set("streams", std::move(streams));
        body.set("random", std::move(random));
    }
    if (m_ctx.state) body.set("state", m_ctx.state->toJson());
    if (m_ctx.storylets) body.set("storylets", m_ctx.storylets->toJson());
    if (m_ctx.params) {
        Json params = Json::object();
        for (const ParamInfo& p : m_ctx.params->params()) {
            if (p.derived) continue;
            Json e = Json::object();
            e.set("value", p.value);
            e.set("target", p.target);
            params.set(p.name, std::move(e));
        }
        body.set("params", std::move(params));
    }
    if (m_ctx.observation) body.set("observation", m_ctx.observation->toJson());
    Json sections = Json::object();
    for (const auto& [name, section] : m_sections) sections.set(name, section.save());
    body.set("sections", std::move(sections));
    return body;
}

bool SaveSystem::apply(const Json& input, std::string* error) {
    Json body = input;
    if (!body.isObject()) return fail(error, "save: body is not an object");
    for (const auto& [name, section] : m_sections) {
        const Json* sections = body.get("sections");
        if (sections && sections->get(name) == nullptr) return fail(error, "save: missing section " + name);
    }

    if (m_ctx.world && body.get("world")) {
        World& w = *m_ctx.world;
        const Json* world = body.get("world");
        const Json* slots = body.get("slots");
        m_remap.clear();
        for (uint32_t i = 0; slots && i < static_cast<uint32_t>(slots->asNumber()); ++i) m_remap.push_back(EntityHandle{i, 0});
        if (!world->isString() || !w.restore(world->asString(), &m_remap)) return fail(error, "save: world restore failed");
        if (const Json* time = body.get("time")) {
            TimeState& t = w.timeState();
            if (const Json* v = time->get("elapsed")) t.elapsed = v->asNumber();
            if (const Json* v = time->get("unscaledElapsed")) t.unscaledElapsed = v->asNumber();
            if (const Json* v = time->get("frame")) t.frame = static_cast<uint64_t>(v->asNumber());
            if (const Json* v = time->get("timeScale")) t.timeScale = static_cast<float>(v->asNumber(1.0));
            if (const Json* v = time->get("paused")) t.paused = v->asBool();
        }
        if (const Json* cam = body.get("camera")) {
            const Json* p = cam->get("position");
            if (p && p->size() == 3) w.camera().setPos({static_cast<float>(p->at(0)->asNumber()), static_cast<float>(p->at(1)->asNumber()), static_cast<float>(p->at(2)->asNumber())});
            const Json* yaw = cam->get("yaw");
            const Json* pitch = cam->get("pitch");
            if (yaw && pitch) w.camera().setOrientation(static_cast<float>(yaw->asNumber()), static_cast<float>(pitch->asNumber()));
        }
        if (const Json* random = body.get("random")) {
            uint64_t seed = 0;
            const Json* s = random->get("seed");
            if (!s || !fromHex(s->asString(), seed)) return fail(error, "save: bad random seed");
            w.random().clear();
            w.random().seed(seed);
            if (const Json* streams = random->get("streams")) {
                for (const auto& [name, e] : streams->members()) {
                    uint64_t st = 0;
                    uint64_t inc = 0;
                    const Json* a = e.get("state");
                    const Json* b = e.get("increment");
                    if (!a || !b || !fromHex(a->asString(), st) || !fromHex(b->asString(), inc)) return fail(error, "save: bad random stream " + name);
                    w.random().stream(name).restore(st, inc);
                }
            }
        }
    }
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
            for (const auto& [name, e] : params->members()) {
                const Json* value = e.get("value");
                const Json* target = e.get("target");
                if (!value || !target) return fail(error, "save: bad param " + name);
                if (!m_ctx.params->has(name)) m_ctx.params->declare(name, static_cast<float>(value->asNumber()));
                m_ctx.params->snap(name, static_cast<float>(value->asNumber()));
                m_ctx.params->set(name, static_cast<float>(target->asNumber()));
            }
        }
    }
    if (m_ctx.observation) {
        const Json* obs = body.get("observation");
        if (!obs || !m_ctx.observation->fromJson(*obs, [this](EntityHandle h) { return remap(h); })) return fail(error, "save: bad observation");
    }
    if (const Json* sections = body.get("sections")) {
        for (const auto& [name, section] : m_sections) {
            const Json* data = sections->get(name);
            if (data && !section.load(*data, error)) return false;
        }
    }
    if (const Json* meta = body.get("meta")) {
        if (const Json* serial = meta->get("serial")) m_serial = static_cast<uint64_t>(serial->asNumber());
    }
    return true;
}

bool SaveSystem::save(const std::string& path, const std::string& label, std::string* error) {
    Json body = capture(label);
    const std::string bodyText = body.dump();
    Json doc = Json::object();
    doc.set("format", "litsave");
    doc.set("version", kSaveVersion);
    doc.set("checksum", toHex(hashName(bodyText)));
    doc.set("body", std::move(body));

    namespace fs = std::filesystem;
    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail(error, "save: cannot write " + tmp);
        const std::string text = doc.dump();
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            fs::remove(tmp, ec);
            return fail(error, "save: write failed " + tmp);
        }
    }
    syncFile(tmp);
    if (fs::exists(target, ec)) {
        fs::copy_file(target, path + ".bak", fs::copy_options::overwrite_existing, ec);
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail(error, "save: rename failed for " + path);
    }
    ++m_serial;
    return true;
}

bool SaveSystem::load(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(error, "save: cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string parseError;
    auto doc = Json::parse(ss.str(), &parseError);
    if (!doc || !doc->isObject()) return fail(error, "save: corrupt file (" + parseError + ")");
    const Json* format = doc->get("format");
    const Json* version = doc->get("version");
    const Json* checksum = doc->get("checksum");
    const Json* body = doc->get("body");
    if (!format || format->asString() != "litsave" || !version || !version->isNumber() || !checksum || !body) return fail(error, "save: not a save file");
    uint64_t expected = 0;
    if (!fromHex(checksum->asString(), expected) || expected != hashName(body->dump())) return fail(error, "save: checksum mismatch");
    int v = static_cast<int>(version->asNumber());
    if (v > kSaveVersion || v < 1) return fail(error, "save: unsupported version " + std::to_string(v));
    Json working = *body;
    for (; v < kSaveVersion; ++v) {
        const auto it = m_migrations.find(v);
        if (it == m_migrations.end()) return fail(error, "save: no migration from version " + std::to_string(v));
        if (!it->second(working, error)) return false;
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

bool SaveSystem::autosave(float dt, std::string* error) {
    if (m_autoDir.empty() || m_autoInterval <= 0.0f) return false;
    m_autoElapsed += dt;
    if (m_autoElapsed < m_autoInterval) return false;
    m_autoElapsed = 0.0f;
    return autosaveNow(error);
}

bool SaveSystem::loadLatestAutosave(std::string* error) {
    if (m_autoDir.empty()) return fail(error, "save: autosave not configured");
    std::vector<std::pair<uint64_t, std::string>> found;
    for (uint32_t slot = 0; slot < m_autoSlots; ++slot) {
        const std::string path = autosavePath(slot);
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        const auto doc = Json::parse(ss.str());
        const Json* body = doc ? doc->get("body") : nullptr;
        const Json* meta = body ? body->get("meta") : nullptr;
        const Json* serial = meta ? meta->get("serial") : nullptr;
        if (serial) found.emplace_back(static_cast<uint64_t>(serial->asNumber()), path);
    }
    std::sort(found.begin(), found.end(), std::greater<>());
    std::string last = "save: no autosave found";
    for (const auto& [serial, path] : found) {
        std::string why;
        if (load(path, &why)) return true;
        last = why;
    }
    return fail(error, last);
}
