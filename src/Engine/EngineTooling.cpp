#include <GLFW/glfw3.h>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <charconv>
#include <filesystem>
#include <ostream>
#include <string_view>
#include <system_error>
#include <type_traits>
#include "Engine/Log/Log.hpp"

import Engine.engine;
import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.Character;
import Engine.CameraRig;
import Engine.Animation;
import Engine.Jobs;
import Engine.Profiler;
import Engine.Render.entity;
import Engine.glm;
import Engine.asset;
import Engine.input;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.Replay;
import Engine.Perf;
import Engine.Params;
import Engine.Json;
import Engine.Dump;
import Engine.Rng;
import Engine.Save;
import Engine.Streaming;
import Engine.Storylet;
import Engine.Observation;
import Engine.Delta;
import Engine.State;
import Engine.Portal;
import Engine.Portal.view;
import Engine.PortalBridge;
import Engine.Render.observation;

bool Engine::startRecording(const std::string& path) {
    m_recordPath = path;
    m_recorder.begin(m_world.random().seed());
    return true;
}

bool Engine::stopRecording() {
    if (!m_recorder.active()) return false;
    m_recorder.stop();
    return m_recorder.save(m_recordPath);
}

bool Engine::startReplay(ReplayData data) {
    m_player.load(std::move(data));
    return m_player.loaded();
}

bool Engine::startReplay(const std::string& path) {
    ReplayData data;
    std::string error;
    if (!loadReplay(path, data, &error)) {
        Lit::Log::Error("Replay '{}' failed: {}", path, error);
        return false;
    }
    return startReplay(std::move(data));
}

void Engine::enablePerfLog(const std::string& path) {
    m_perf.open(path);
    m_lastTickStart = {};
}

void Engine::disablePerfLog() { m_perf.close(); }

Json Engine::dumpState() {
    Json out = Json::object();
    out.set("world", dumpWorld(m_world));
    out.set("random", dumpRandom(m_world.random()));
    Json params = Json::object();
    for (const ParamInfo& p : m_params.params()) params.set(p.name, p.value);
    out.set("params", std::move(params));
    return out;
}

std::string Engine::dumpStateText(bool pretty) { return dumpState().dump(pretty ? 2 : -1); }

bool Engine::writeStateDump(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << dumpStateText(true) << '\n';
    return static_cast<bool>(out);
}

bool Engine::loadPostFx(const std::string& path, std::string* error) {
    const std::optional<std::string> fileText = readTextFile(path);
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (!fileText) return fail("cannot open " + path);
    const std::string& text = *fileText;
    std::string parseError;
    const std::optional<Json> doc = Json::parse(text, &parseError);
    if (!doc) return fail("json: " + parseError);
    if (!doc->isObject()) return fail("root must be an object");
    std::vector<PostFxBinding> bindings;
    RenderSettings probe;
    for (const auto& [key, definition] : doc->members()) {
        bool known = false;
        visitRenderSettings(probe, [&](const char* name, auto&) { known = known || key == name; });
        PostFxBinding binding;
        binding.key = key;
        const Json* value = &definition;
        if (definition.isObject()) {
            value = definition.get("base");
            if (const Json* param = definition.get("param")) binding.param = param->asString();
        }
        if (value && value->isNumber()) {
            binding.base[0] = static_cast<float>(value->asNumber());
        } else if (value && value->isArray() && value->size() == 3) {
            binding.count = 3;
            for (size_t channel = 0; channel < 3; ++channel) binding.base[channel] = static_cast<float>(value->items()[channel].asNumber());
        } else if (!binding.param.empty()) {
            binding.base[0] = 0.0f;
        } else {
            return fail(key + ": expected number, [r,g,b] or {base,param}");
        }
        if (!known) return fail("unknown render setting " + key);
        bindings.push_back(std::move(binding));
    }
    m_postFx = std::move(bindings);
    applyPostFx();
    const bool watched = std::any_of(m_dataWatches.begin(), m_dataWatches.end(), [&](const DataWatch& watch) { return watch.path == path; });
    if (!watched) {
        watchData(path, [this](const std::string& file) {
            std::string why;
            if (loadPostFx(file, &why)) return true;
            Lit::Log::Error("Post fx reload failed: {}", why);
            return false;
        });
    }
    return true;
}

void Engine::applyPostFx() {
    if (m_postFx.empty()) return;
    RenderSettings& settings = m_renderer.settings();
    for (const PostFxBinding& binding : m_postFx) {
        float value = binding.base[0];
        if (!binding.param.empty() && m_params.has(binding.param)) value = m_params.get(binding.param);
        visitRenderSettings(settings, [&](const char* name, auto& field) {
            if (binding.key != name) return;
            using T = std::remove_cvref_t<decltype(field)>;
            if constexpr (std::is_same_v<T, float>) field = value;
            else if constexpr (std::is_same_v<T, glm::vec3>) field = glm::vec3(binding.base[0], binding.base[1], binding.base[2]);
        });
    }
}

void Engine::watchData(const std::string& path, std::function<bool(const std::string&)> reload) {
    std::error_code ec;
    DataWatch watch;
    watch.path = path;
    watch.stamp = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
    watch.reload = std::move(reload);
    m_dataWatches.push_back(std::move(watch));
}

void Engine::pollData() {
    std::error_code ec;
    for (DataWatch& watch : m_dataWatches) {
        const long long stamp = std::filesystem::last_write_time(watch.path, ec).time_since_epoch().count();
        if (ec || stamp == watch.stamp) continue;
        watch.stamp = stamp;
        if (watch.reload(watch.path)) {
            ++m_dataReloads;
            Lit::Log::Info("Reloaded {}", watch.path);
        }
    }
}

std::vector<std::string> Engine::watchedFiles() const {
    std::vector<std::string> out;
    if (!m_params.watchedPath().empty()) out.push_back(m_params.watchedPath());
    for (const DataWatch& watch : m_dataWatches) out.push_back(watch.path);
    return out;
}

void Engine::uploadParams() {
    if (m_paramsUploaded == m_params.version()) return;
    m_paramsUploaded = m_params.version();
    m_renderer.custom().setBuffer("EngineParams", sizeof(float), kParamSlots, m_params.values());
}

