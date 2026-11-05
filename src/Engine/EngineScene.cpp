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

namespace {

template <typename M, typename F>
void visitMaterialNumbers(M& m, F&& f) {
    for (int i = 0; i < 3; ++i) f(m.baseColor[i]);
    f(m.opacity);
    f(m.metallic);
    f(m.roughness);
    f(m.reflectance);
    f(m.occlusion);
    for (int i = 0; i < 3; ++i) f(m.emissive[i]);
    f(m.emissiveIntensity);
    f(m.clearcoat);
    f(m.clearcoatRoughness);
    f(m.anisotropy);
    f(m.anisotropyRotation);
    for (int i = 0; i < 3; ++i) f(m.sheenColor[i]);
    f(m.sheenRoughness);
    for (int i = 0; i < 3; ++i) f(m.subsurfaceColor[i]);
    f(m.subsurface);
    f(m.transmission);
    f(m.ior);
    f(m.thickness);
    f(m.normalStrength);
    f(m.uvScale);
    f(m.triplanar);
    f(m.baseColorTexture);
    f(m.normalTexture);
    f(m.ormTexture);
    f(m.emissiveTexture);
    for (int i = 0; i < 4; ++i) f(m.custom0[i]);
    for (int i = 0; i < 4; ++i) f(m.custom1[i]);
}

bool nextNumber(std::string_view& text, float& out) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (error != std::errc{}) return false;
    text.remove_prefix(static_cast<size_t>(end - text.data()));
    return true;
}

}

void Engine::registerSceneSections() {
    m_world.addSceneSection(
        "texture",
        [this](std::ostream& out) {
            for (int srgb = 0; srgb < 2; ++srgb) {
                for (size_t i = 0; i < m_texturePaths[srgb].size(); ++i) {
                    if (!m_texturePaths[srgb][i].empty()) out << "texture " << srgb << ' ' << i << '\t' << m_texturePaths[srgb][i] << '\n';
                }
            }
        },
        [this](std::string_view line) {
            const size_t tab = line.find('\t');
            if (tab == std::string_view::npos) return;
            std::string_view head = line.substr(0, tab);
            float srgb = 0.0f, index = 0.0f;
            if (!nextNumber(head, srgb) || !nextNumber(head, index)) return;
            const int kind = srgb > 0.5f ? 1 : 0;
            const int loaded = loadTexture(std::string(line.substr(tab + 1)), kind == 1);
            if (loaded >= 0) m_textureRemap[kind][static_cast<int>(index)] = loaded;
        },
        [this] {
            m_textureRemap[0].clear();
            m_textureRemap[1].clear();
        });
    m_world.addSceneSection(
        "surface",
        [this](std::ostream& out) {
            const std::vector<std::string> names = m_renderer.surfaceNames();
            for (uint32_t id = 0; id < names.size(); ++id) out << "surface " << id << '\t' << names[id] << '\t' << m_renderer.surfacePath(id) << '\n';
        },
        [this](std::string_view line) {
            const size_t first = line.find('\t');
            const size_t second = first == std::string_view::npos ? first : line.find('\t', first + 1);
            if (second == std::string_view::npos) return;
            std::string_view idText = line.substr(0, first);
            float saved = 0.0f;
            if (!nextNumber(idText, saved)) return;
            const std::string name(line.substr(first + 1, second - first - 1));
            const std::vector<std::string> names = m_renderer.surfaceNames();
            uint32_t id = 0;
            const auto known = std::find(names.begin(), names.end(), name);
            if (known != names.end()) {
                id = static_cast<uint32_t>(known - names.begin());
            } else {
                id = loadShader(name, std::string(line.substr(second + 1)));
            }
            if (id != static_cast<uint32_t>(saved)) m_surfaceRemap[static_cast<uint32_t>(saved)] = id;
        },
        [this] { m_surfaceRemap.clear(); },
        [this] {
            if (m_surfaceRemap.empty()) return;
            m_world.forEach([&](EntityHandle e) {
                const auto it = m_surfaceRemap.find(m_world.getRenderable(e).shaderId);
                if (it != m_surfaceRemap.end()) m_world.setShader(e, it->second);
            });
            m_surfaceRemap.clear();
        });
    m_world.addSceneSection(
        "material",
        [this](std::ostream& out) {
            out.precision(9);
            for (size_t id = 0; id < m_materials.size(); ++id) {
                out << "material " << id;
                visitMaterialNumbers(m_materials[id], [&](const auto& value) { out << ' ' << static_cast<float>(value); });
                out << '\t' << m_materials[id].name << '\n';
            }
        },
        [this](std::string_view line) {
            const size_t tab = line.find('\t');
            std::string_view numbers = line.substr(0, tab);
            float idValue = 0.0f;
            if (!nextNumber(numbers, idValue) || idValue < 0.0f || idValue >= 1024.0f) return;
            const auto id = static_cast<uint32_t>(idValue);
            Material m;
            bool ok = true;
            visitMaterialNumbers(m, [&](auto& field) {
                float value = 0.0f;
                ok = ok && nextNumber(numbers, value);
                if (ok) field = static_cast<std::remove_reference_t<decltype(field)>>(value);
            });
            if (!ok) return;
            if (tab != std::string_view::npos) m.name = std::string(line.substr(tab + 1));
            const auto remap = [](const std::unordered_map<int, int>& table, int& texture) {
                if (texture < 0) return;
                const auto it = table.find(texture);
                texture = it == table.end() ? NO_TEXTURE : it->second;
            };
            remap(m_textureRemap[1], m.baseColorTexture);
            remap(m_textureRemap[1], m.emissiveTexture);
            remap(m_textureRemap[0], m.normalTexture);
            remap(m_textureRemap[0], m.ormTexture);
            while (m_materials.size() <= id) createMaterial(Material{});
            setMaterial(id, m);
        });
    m_world.addSceneSection(
        "render",
        [this](std::ostream& out) {
            if (m_renderer.initialized()) writeRenderSettings(m_renderer.settings(), out, "render ");
        },
        [this](std::string_view line) {
            const size_t space = line.find(' ');
            if (!m_renderer.initialized()) return;
            if (space != std::string_view::npos) setRenderSetting(m_renderer.settings(), line.substr(0, space), line.substr(space + 1));
        });
    m_world.addSceneSection(
        "pass", [this](std::ostream& out) { m_renderer.custom().save(out, "pass "); }, [this](std::string_view line) { m_renderer.custom().load(line); });
}

int Engine::loadTexture(const std::string& path, bool srgb) {
    auto& paths = m_texturePaths[srgb ? 1 : 0];
    for (size_t i = 0; i < paths.size(); ++i) {
        if (paths[i] == path) return static_cast<int>(i);
    }
    const int index = m_renderer.loadTexture(path, srgb);
    if (index < 0) return index;
    if (paths.size() <= static_cast<size_t>(index)) paths.resize(static_cast<size_t>(index) + 1);
    paths[static_cast<size_t>(index)] = path;
    return index;
}

namespace {
constexpr const char* kModelDirectory = "resources/models/";

bool isSafeMeshName(const std::string& name) {
    return !(name.empty() || name.size() > 128 || name.find("..") != std::string::npos || name.front() == '/' || name.find('\\') != std::string::npos || name.find('\0') != std::string::npos);
}

std::string meshSourcePath(const std::string& name) { return kModelDirectory + name + ".obj"; }
}

uint32_t Engine::registerProcessedMesh(const std::string& name, const ProcessedMesh& processed) {
    const uint32_t id = uploadProcessedMesh(processed.base, processed.lods);
    m_meshIds[name] = id;
    m_meshNames[id] = name;
    return id;
}

uint32_t Engine::loadMesh(const std::string& name) {
    if (!isSafeMeshName(name)) {
        Lit::Log::Warn("Rejected unsafe mesh name '{}'", name);
        return 0;
    }
    if (const auto it = m_meshIds.find(name); it != m_meshIds.end()) return it->second;

    const std::string source = meshSourcePath(name);
    const auto processed = AssetManager::loadProcessed(source, CacheOptions{});
    if (!processed) {
        Lit::Log::Warn("Failed to load {}", source);
        return 0;
    }
    return registerProcessedMesh(name, *processed);
}

void Engine::preloadMeshes(const std::vector<std::string>& names) {
    std::vector<std::string> pending;
    std::vector<std::string> sources;
    for (const std::string& name : names) {
        if (!isSafeMeshName(name) || m_meshIds.contains(name)) continue;
        if (std::find(pending.begin(), pending.end(), name) != pending.end()) continue;
        pending.push_back(name);
        sources.push_back(meshSourcePath(name));
    }
    const auto results = AssetManager::loadProcessedMany(sources, CacheOptions{}, m_jobs);
    for (size_t slot = 0; slot < pending.size(); ++slot) {
        if (results[slot]) registerProcessedMesh(pending[slot], *results[slot]);
        else Lit::Log::Warn("Failed to load {}", sources[slot]);
    }
}

void Engine::preloadSceneMeshes(const std::string& scenePath) {
    constexpr std::string_view kMeshLinePrefix = "mesh ";
    std::ifstream scene(scenePath);
    std::vector<std::string> names;
    std::string line;
    while (std::getline(scene, line)) {
        if (line.compare(0, kMeshLinePrefix.size(), kMeshLinePrefix) != 0) continue;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) names.push_back(line.substr(tab + 1));
    }
    preloadMeshes(names);
}

std::string Engine::meshName(uint32_t id) const {
    const auto it = m_meshNames.find(id);
    return it == m_meshNames.end() ? std::string() : it->second;
}

std::vector<std::string> Engine::availableMeshes() const {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("resources/models", ec)) {
        if (entry.path().extension() == ".obj") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}


