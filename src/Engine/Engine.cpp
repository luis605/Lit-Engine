#include <GLFW/glfw3.h>
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

Engine::Engine() {
    m_world.setJobSystem(&m_jobs);
    m_world.setProfiler(&m_profiler);
    m_world.setMeshHooks(
        [this](uint32_t id) {
            const auto it = m_meshNames.find(id);
            return it == m_meshNames.end() ? std::string() : it->second;
        },
        [this](const std::string& name) { return loadMesh(name); });
    m_world.setMeshBoundsHook([this](uint32_t id) { return m_renderer.getMeshBounds(id); });
    registerCoreComponents(m_world);
    registerSceneSections();
    addSystem(Phase::Update, "animation", [this](World& world, float dt) { updateAnimators(world, m_animations, dt); });
    addSystem(Phase::FixedUpdate, "physics", [this](World& world, float dt) { stepPhysics(world, dt, m_physics); });
}

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

}  // namespace

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

uint32_t Engine::loadMesh(const std::string& name) {
    if (name.empty() || name.size() > 128 || name.find("..") != std::string::npos || name.front() == '/' || name.find('\\') != std::string::npos || name.find('\0') != std::string::npos) {
        Lit::Log::Warn("Rejected unsafe mesh name '{}'", name);
        return 0;
    }
    if (const auto it = m_meshIds.find(name); it != m_meshIds.end()) return it->second;

    const std::string source = "resources/models/" + name + ".obj";
    const std::string asset = "resources/assets/" + name + ".asset";
    std::filesystem::create_directories("resources/assets");
    if (!AssetManager::bake(source, asset)) { Lit::Log::Warn("Failed to bake {}", source); }
    auto mesh = AssetManager::load(asset);
    if (!mesh) {
        Lit::Log::Warn("Failed to load {}", asset);
        return 0;
    }
    const uint32_t id = uploadMesh(*mesh);
    m_meshIds[name] = id;
    m_meshNames[id] = name;
    return id;
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

Engine::~Engine() { cleanup(); }

bool Engine::open(const WindowConfig& config) {
    Lit::Log::Init();
    if (!glfwInit()) {
        Lit::Log::Fatal("Failed to initialize GLFW");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!window) {
        Lit::Log::Fatal("Failed to create window '{}'", config.title);
        glfwTerminate();
        return false;
    }
    Lit::Log::Phase("window");
    m_ownsWindow = true;
    InputManager::Init(window);
    init(window, config.width, config.height);
    return true;
}

int Engine::run(const std::function<void(float)>& frame) {
    double last = glfwGetTime();
    while (m_window && !glfwWindowShouldClose(m_window)) {
        const double now = glfwGetTime();
        const auto deltaTime = static_cast<float>(now - last);
        last = now;
        beginUi();
        if (frame) frame(deltaTime);
        tick(deltaTime);
        update();
        InputManager::Update();
        present();
        glfwPollEvents();
    }
    cleanup();
    return 0;
}

bool Engine::enableImGui() { return m_renderer.enableImGui(); }

void Engine::beginUi() { m_renderer.beginUi(); }

void Engine::setMouseCaptured(bool captured) {
    m_mouseCaptured = captured;
    if (m_window) glfwSetInputMode(m_window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Engine::enableFlyCamera(const FlyCameraSettings& settings) {
    addSystem(Phase::Update, "fly camera", [this, settings](World& world, float) {
        if (InputManager::IsKeyPressed(settings.captureMouseKey)) setMouseCaptured(!m_mouseCaptured);
        Camera& camera = world.camera();
        const float step = settings.speed * (InputManager::IsKeyHeld(GLFW_KEY_LEFT_SHIFT) ? settings.sprintMultiplier : 1.0f) * world.time().unscaledDeltaTime;
        if (InputManager::IsKeyHeld(GLFW_KEY_W)) camera.processKeyboard(CameraMovement::FORWARD, step);
        if (InputManager::IsKeyHeld(GLFW_KEY_S)) camera.processKeyboard(CameraMovement::BACKWARD, step);
        if (InputManager::IsKeyHeld(GLFW_KEY_A)) camera.processKeyboard(CameraMovement::LEFT, step);
        if (InputManager::IsKeyHeld(GLFW_KEY_D)) camera.processKeyboard(CameraMovement::RIGHT, step);
        if (m_mouseCaptured) {
            const glm::vec2 delta = InputManager::GetMouseDelta();
            camera.processMouseMovement(delta.x, -delta.y);
        }
    }, true);
}

void Engine::init(GLFWwindow* window, const int windowWidth, const int windowHeight) {
    m_window = window;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
    m_world.camera().updateAspectRatio(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    m_renderer.init(window, windowWidth, windowHeight);
}

std::optional<RayHit> Engine::pick(float screenX, float screenY) {
    m_world.syncCamera();
    const Ray ray = m_world.screenRay(screenX, screenY, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight));
    return m_world.raycast(ray.origin, ray.direction);
}

void Engine::update(SceneDatabase& sceneDatabase, Camera& camera) {
    m_renderer.drawScene(sceneDatabase, camera);
}

void Engine::addSystem(Phase phase, std::string name, SystemFn fn, bool runWhenPaused) {
    removeSystem(name);
    m_systems.push_back({phase, std::move(name), std::move(fn), true, runWhenPaused});
}

bool Engine::removeSystem(const std::string& name) {
    const auto it = std::find_if(m_systems.begin(), m_systems.end(), [&](const System& s) { return s.name == name; });
    if (it == m_systems.end()) return false;
    m_systems.erase(it);
    return true;
}

bool Engine::setSystemEnabled(const std::string& name, bool enabled) {
    const auto it = std::find_if(m_systems.begin(), m_systems.end(), [&](const System& s) { return s.name == name; });
    if (it == m_systems.end()) return false;
    it->enabled = enabled;
    return true;
}

bool Engine::isSystemEnabled(const std::string& name) const {
    const auto it = std::find_if(m_systems.begin(), m_systems.end(), [&](const System& s) { return s.name == name; });
    return it != m_systems.end() && it->enabled;
}

std::vector<std::string> Engine::systemNames(Phase phase) const {
    std::vector<std::string> names;
    for (const System& s : m_systems) {
        if (s.phase == phase) names.push_back(s.name);
    }
    return names;
}

void Engine::runSystems(Phase phase, float dt, bool paused) {
    for (size_t i = 0; i < m_systems.size(); ++i) {
        if (m_systems[i].phase != phase || !m_systems[i].enabled) continue;
        if (paused && !m_systems[i].runWhenPaused) continue;
        const SystemFn fn = m_systems[i].fn;
        ProfileScope profile(m_profiler, m_systems[i].name);
        fn(m_world, dt);
    }
}

void Engine::tick(float deltaTime) {
    m_profiler.beginFrame();
    ProfileScope tickProfile(m_profiler, "Engine::tick");
    constexpr float maxFrame = 0.25f;
    TimeState& t = m_world.timeState();
    t.unscaledDeltaTime = std::min(deltaTime, maxFrame);
    t.deltaTime = t.paused ? 0.0f : t.unscaledDeltaTime * t.timeScale;
    t.unscaledElapsed += t.unscaledDeltaTime;
    t.elapsed += t.deltaTime;
    ++t.frame;

    if (!t.paused) {
        runSystems(Phase::PreUpdate, t.deltaTime, false);
        m_accumulator += t.deltaTime;
        while (m_accumulator >= m_fixedStep) {
            m_world.fixedUpdate(m_fixedStep);
            runSystems(Phase::FixedUpdate, m_fixedStep, false);
            m_accumulator -= m_fixedStep;
        }
        {
            ProfileScope scriptsProfile(m_profiler, "scripts+timers");
            m_world.update(t.deltaTime);
        }
        runSystems(Phase::Update, t.deltaTime, false);
        runSystems(Phase::PostUpdate, t.deltaTime, false);
    } else {
        runSystems(Phase::PreUpdate, 0.0f, true);
        runSystems(Phase::Update, 0.0f, true);
        runSystems(Phase::PostUpdate, 0.0f, true);
    }
    m_world.events().dispatch();
}

void Engine::reloadShaders() { m_renderer.reloadShaders(); }

bool Engine::shadersChanged() {
    bool changed = false;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("resources/shaders", ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const long long stamp = entry.last_write_time(ec).time_since_epoch().count();
        const std::string key = entry.path().string();
        const auto it = m_shaderStamps.find(key);
        if (it == m_shaderStamps.end()) {
            m_shaderStamps.emplace(key, stamp);
        } else if (it->second != stamp) {
            it->second = stamp;
            changed = true;
        }
    }
    return changed;
}

void Engine::update() {
    ProfileScope updateProfile(m_profiler, "Engine::update");
    if (m_shaderWatch) {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastShaderPoll > std::chrono::milliseconds(500)) {
            m_lastShaderPoll = now;
            if (shadersChanged()) m_renderer.reloadShaders();
        }
    }
    m_world.syncCamera();
    LightSet lights;
    const bool hasLights = collectLights(m_world, m_world.camera().getPosition(), lights, 1024);
    m_renderer.setLights(hasLights, lights.directional, lights.packed);
    syncWindowSize(m_world.camera());
    m_renderer.drawScene(m_world.database(), m_world.camera());
}

void Engine::update(World& world) {
    world.syncCamera();
    LightSet lights;
    const bool hasLights = collectLights(world, world.camera().getPosition(), lights, 1024);
    m_renderer.setLights(hasLights, lights.directional, lights.packed);
    syncWindowSize(world.camera());
    m_renderer.drawScene(world.database(), world.camera());
}

void Engine::present() { m_renderer.present(); }

void Engine::syncWindowSize(Camera& camera) {
    if (!m_renderer.resizeToWindow()) return;
    m_windowWidth = m_renderer.width();
    m_windowHeight = m_renderer.height();
    camera.updateAspectRatio(static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight));
}

void Engine::cleanup() {
    m_renderer.cleanup();
    if (m_ownsWindow && m_window) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
    m_window = nullptr;
    m_ownsWindow = false;
}

uint32_t Engine::uploadMesh(const Mesh& mesh) { return m_renderer.uploadMesh(mesh); }

void Engine::setLodBias(float bias) { m_renderer.setLodBias(bias); }
void Engine::setForcedLod(int lod) { m_renderer.setForcedLod(lod); }
int Engine::getForcedLod() const { return m_renderer.getForcedLod(); }

void Engine::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_renderer.AddText(text, x, y, scale, color);
}

void Engine::setSmallObjectThreshold(float threshold) { m_renderer.setSmallObjectThreshold(threshold); }
void Engine::setDebugDepthMode(bool enabled) { m_renderer.setDebugDepthMode(enabled); }
bool Engine::isDebugDepthMode() const { return m_renderer.isDebugDepthMode(); }
void Engine::setFullProfiling(bool enabled) { m_renderer.setFullProfiling(enabled); }

void Engine::uploadBasePositions(const std::vector<glm::vec3>& basePositions) {
    m_renderer.uploadBasePositions(basePositions);
    m_animationBase = basePositions;
    applyWorldAnimation();
}

void Engine::setAnimation(float time, uint32_t movingCount, uint32_t entityOffset) {
    m_renderer.setAnimation(time, movingCount, entityOffset);
    m_world.setAnimationTime(time);
    if (entityOffset != m_animationOffset || movingCount != m_animationCount) {
        m_animationOffset = entityOffset;
        m_animationCount = movingCount;
        applyWorldAnimation();
    }
}
void Engine::debugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool overlay) { m_renderer.addDebugLine(from, to, color, overlay); }

void Engine::debugBox(const glm::vec3& c, const glm::vec3& h, const glm::vec4& color, bool overlay) {
    const glm::vec3 corners[8] = {
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z}};
    constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : edges) debugLine(corners[e[0]], corners[e[1]], color, overlay);
}

void Engine::debugSphere(const glm::vec3& center, float radius, const glm::vec4& color) {
    constexpr int segments = 24;
    constexpr float step = 6.28318530718f / segments;
    for (int i = 0; i < segments; ++i) {
        const float a0 = step * i, a1 = step * (i + 1);
        const float c0 = std::cos(a0) * radius, s0 = std::sin(a0) * radius;
        const float c1 = std::cos(a1) * radius, s1 = std::sin(a1) * radius;
        debugLine(center + glm::vec3(c0, s0, 0.0f), center + glm::vec3(c1, s1, 0.0f), color);
        debugLine(center + glm::vec3(c0, 0.0f, s0), center + glm::vec3(c1, 0.0f, s1), color);
        debugLine(center + glm::vec3(0.0f, c0, s0), center + glm::vec3(0.0f, c1, s1), color);
    }
}

void Engine::debugRay(const Ray& ray, float length, const glm::vec4& color) { debugLine(ray.origin, ray.origin + ray.direction * length, color); }

void Engine::debugHierarchy(const glm::vec4& color) {
    m_world.forEach([&](EntityHandle e) {
        const EntityHandle parent = m_world.getParent(e);
        if (!parent.isNull()) debugLine(m_world.getWorldPosition(parent), m_world.getWorldPosition(e), color);
    });
}

void Engine::applyWorldAnimation() {
    const size_t count = std::min<size_t>(m_animationCount, m_animationBase.size());
    m_world.setAnimation(m_animationOffset, std::vector<glm::vec3>(m_animationBase.begin(), m_animationBase.begin() + count));
}

uint32_t Engine::createMaterial(const glm::vec3& color, float strength) {
    Material m;
    m.name = "Material " + std::to_string(m_materials.size());
    m.baseColor = color * std::clamp(strength, 0.0f, 1.0f) + glm::vec3(0.8f) * (1.0f - std::clamp(strength, 0.0f, 1.0f));
    return createMaterial(m);
}

void Engine::updateMaterial(uint32_t id, const glm::vec3& color, float strength) {
    if (id >= m_materials.size()) return;
    Material m = m_materials[id];
    m.baseColor = color * std::clamp(strength, 0.0f, 1.0f) + glm::vec3(0.8f) * (1.0f - std::clamp(strength, 0.0f, 1.0f));
    setMaterial(id, m);
}

uint32_t Engine::createMaterial(const Material& material) {
    constexpr uint32_t maxMaterials = 1024;
    if (m_materials.size() >= maxMaterials) return 0;
    const auto id = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(material);
    uploadMaterial(id);
    return id;
}

void Engine::setMaterial(uint32_t id, const Material& material) {
    if (id >= m_materials.size()) return;
    m_materials[id] = material;
    uploadMaterial(id);
}

const Material& Engine::material(uint32_t id) const { return m_materials[id < m_materials.size() ? id : 0]; }

void Engine::uploadMaterial(uint32_t id) {
    const Material& m = m_materials[id];
    m_renderer.setMaterial(id, m);
}

uint32_t Engine::loadShader(const std::string& name, const std::string& path) {
    const std::string resolved = path.empty() ? "resources/shaders/surfaces/" + name + ".surface" : path;
    return m_renderer.loadSurfaceShader(name, resolved);
}

Ray Engine::screenRay(float screenX, float screenY) {
    m_world.syncCamera();
    return m_world.screenRay(screenX, screenY, static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight));
}

std::optional<glm::vec2> Engine::worldToScreen(const glm::vec3& point) {
    m_world.syncCamera();
    const glm::vec4 clip = m_world.camera().getProjectionMatrix() * m_world.camera().getViewMatrix() * glm::vec4(point, 1.0f);
    if (clip.w <= 1.0e-5f) return std::nullopt;
    const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
    return glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(m_windowWidth), (0.5f - ndc.y * 0.5f) * static_cast<float>(m_windowHeight));
}
