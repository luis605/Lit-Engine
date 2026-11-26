#include <format>
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
#include <cstdlib>
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
import Engine.IK;
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
import Engine.Render.lighting;

namespace {
constexpr size_t kMaxSceneLights = 32768;

std::vector<bool> portalOwningCells(const PortalSystem& portals) {
    std::vector<bool> owning(portals.cells().size(), false);
    for (const Portal& portal : portals.portals()) {
        if (portal.cell >= 0 && static_cast<size_t>(portal.cell) < owning.size()) owning[static_cast<size_t>(portal.cell)] = true;
    }
    return owning;
}

struct PortalLightPlan {
    std::vector<int32_t> cells;
    uint32_t first = 0;
    std::vector<LightAperture> apertures;
};

constexpr float kApertureSoftness = 0.03f;

PortalLightPlan portalLights(const PortalSystem& portals, LightSet& lights) {
    PortalLightPlan plan;
    if (!portals.active()) return plan;
    const std::vector<bool> owning = portalOwningCells(portals);
    const size_t count = lights.packed.size() / 4;
    plan.cells.reserve(count);
    for (size_t light = 0; light < count; ++light) {
        const int cell = portals.cellAt(glm::vec3(lights.packed[light * 4]));
        plan.cells.push_back(cell >= 0 && static_cast<size_t>(cell) < owning.size() && owning[static_cast<size_t>(cell)] ? cell : -1);
    }
    plan.first = static_cast<uint32_t>(count);
    std::vector<glm::vec4> images;
    for (size_t light = 0; light < count; ++light) {
        const int cell = plan.cells[light];
        if (cell < 0) continue;
        const glm::vec4 positionRange = lights.packed[light * 4];
        for (const PortalLightImage& image : portals.lightImages(cell, glm::vec3(positionRange), positionRange.w)) {
            if (plan.apertures.size() >= kMaxPortalLights || count + plan.apertures.size() >= kMaxSceneLights) break;
            const glm::vec4 direction = lights.packed[light * 4 + 2];
            images.push_back(glm::vec4(glm::vec3(image.transform * glm::vec4(glm::vec3(positionRange), 1.0f)), positionRange.w));
            images.push_back(lights.packed[light * 4 + 1]);
            images.push_back(glm::vec4(glm::mat3(image.transform) * glm::vec3(direction), direction.w));
            images.push_back(lights.packed[light * 4 + 3]);
            plan.cells.push_back(image.cell);
            LightAperture aperture;
            aperture.center = glm::vec4(image.center, kApertureSoftness);
            aperture.axisU = glm::vec4(image.axisU, 0.0f);
            aperture.axisV = glm::vec4(image.axisV, 0.0f);
            aperture.plane = glm::vec4(image.normal, -glm::dot(image.normal, image.center));
            plan.apertures.push_back(aperture);
        }
    }
    const std::vector<glm::vec4> collected(lights.packed.begin(), lights.packed.begin() + static_cast<std::ptrdiff_t>(count * 4));
    const std::vector<int32_t> collectedCells(plan.cells.begin(), plan.cells.begin() + static_cast<std::ptrdiff_t>(count));
    constexpr float kSpillReflectance = 0.25f / 3.14159265f;
    constexpr float kSpillOuterCos = 0.17f;
    constexpr float kSpillInnerCos = 0.8f;
    for (size_t id = 0; id < portals.portals().size(); ++id) {
        const Portal& portal = portals.portals()[id];
        if (portal.look.spill <= 0.0f || portal.cell < 0 || static_cast<size_t>(portal.cell) >= owning.size()) continue;
        if (plan.apertures.size() >= kMaxPortalLights || count + plan.apertures.size() >= kMaxSceneLights) break;
        const glm::vec3 irradiance = portals.spillIrradiance(static_cast<int>(id), collected, collectedCells, glm::vec3(lights.directional[0]), glm::vec3(lights.directional[1]));
        const float peak = std::max(irradiance.x, std::max(irradiance.y, irradiance.z));
        if (!(peak > 1.0e-3f)) continue;
        const float minHalf = std::min(portal.half.x, portal.half.y);
        const float setBack = 0.5f * minHalf;
        const glm::vec3 position = portal.center() - portal.normal() * setBack;
        images.push_back(glm::vec4(position, 3.0f * std::max(portal.half.x, portal.half.y) + setBack));
        images.push_back(glm::vec4(irradiance / peak, kSpillReflectance * peak * 4.0f * portal.half.x * portal.half.y));
        images.push_back(glm::vec4(portal.normal(), kSpillOuterCos));
        images.push_back(glm::vec4(kSpillInnerCos, 1.0f, 0.6f * minHalf, 0.0f));
        plan.cells.push_back(portal.cell);
        LightAperture aperture;
        aperture.center = glm::vec4(portal.center(), kApertureSoftness);
        aperture.axisU = glm::vec4(portal.right() * portal.half.x, 0.0f);
        aperture.axisV = glm::vec4(portal.up() * portal.half.y, 0.0f);
        aperture.plane = glm::vec4(portal.normal(), -glm::dot(portal.normal(), portal.center()));
        plan.apertures.push_back(aperture);
    }
    lights.packed.insert(lights.packed.end(), images.begin(), images.end());
    lights.count = static_cast<uint32_t>(lights.packed.size() / 4);
    return plan;
}

void uploadLights(Renderer& renderer, const PortalSystem& portals, bool hasLights, LightSet& lights) {
    const PortalLightPlan plan = portalLights(portals, lights);
    renderer.setLights(hasLights, lights.directional, lights.packed);
    renderer.setLightCells(plan.cells);
    renderer.setLightApertures(plan.first, plan.apertures);
}
}

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
    registerIkComponents(m_world);
    registerSceneSections();
    m_input.setSource(&m_simInput);
    addSystem(
        Phase::PreUpdate, "params",
        [this](World& world, float) {
            if ((++m_paramPollCounter & 15u) == 0) {
                std::string error;
                if (m_params.pollReload(&error)) Lit::Log::Info("Params reloaded from {}", m_params.watchedPath());
                else if (!error.empty()) Lit::Log::Error("Params reload failed: {}", error);
            }
            if ((m_paramPollCounter & 15u) == 0) pollData();
            m_params.update(world.time().unscaledDeltaTime);
            applyPostFx();
        },
        true);
    addSystem(Phase::Update, "animation", [this](World& world, float dt) { updateAnimators(world, m_animations, dt); });
    addSystem(Phase::Update, "walkers", [](World& world, float dt) { updateWalkers(world, dt); });
    addSystem(Phase::Update, "ik", [](World& world, float dt) { updateIk(world, dt); });
    installPortalPhysics(m_portals);
    addSystem(Phase::FixedUpdate, "physics", [this](World& world, float dt) {
        if (m_portals.active()) stepPortalPhysics(m_portals, world, dt, m_physics);
        else stepPhysics(world, dt, m_physics);
    });
}

Engine::~Engine() { cleanup(); }

bool Engine::open(const WindowConfig& config) {
    Lit::Log::Init();
    if (!glfwInit()) {
        Lit::Log::Fatal("Failed to initialize GLFW");
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (const char* fixed = std::getenv("LIT_FIXED_WINDOW"); fixed && fixed[0] == '1') glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
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
    const char* fixedStep = std::getenv("LIT_FIXED_DT");
    const float fixedDelta = fixedStep ? std::strtof(fixedStep, nullptr) : 0.0f;
    while (m_window && !glfwWindowShouldClose(m_window)) {
        const double now = glfwGetTime();
        const auto deltaTime = fixedDelta > 0.0f ? fixedDelta : static_cast<float>(now - last);
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

bool Engine::openHeadless() {
    m_headless = true;
    m_window = nullptr;
    return true;
}

int Engine::runHeadless(uint64_t ticks, float dt, const std::function<void(uint64_t)>& beforeTick) {
    m_headless = true;
    const float step = dt > 0.0f ? dt : m_fixedStep;
    const uint64_t count = ticks == 0 && m_player.loaded() ? m_player.length() - m_player.position() : ticks;
    for (uint64_t i = 0; i < count; ++i) {
        if (beforeTick) beforeTick(i);
        tick(step);
    }
    if (m_recorder.active()) stopRecording();
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
        const SimInput& in = m_simInput;
        if (in.pressed(settings.captureMouseKey)) setMouseCaptured(!m_mouseCaptured);
        Camera& camera = world.camera();
        const float step = settings.speed * (in.held(GLFW_KEY_LEFT_SHIFT) ? settings.sprintMultiplier : 1.0f) * world.time().unscaledDeltaTime;
        if (in.held(GLFW_KEY_W)) camera.processKeyboard(CameraMovement::FORWARD, step);
        if (in.held(GLFW_KEY_S)) camera.processKeyboard(CameraMovement::BACKWARD, step);
        if (in.held(GLFW_KEY_A)) camera.processKeyboard(CameraMovement::LEFT, step);
        if (in.held(GLFW_KEY_D)) camera.processKeyboard(CameraMovement::RIGHT, step);
        if (m_mouseCaptured) {
            const glm::vec2 delta = in.current.mouseDelta;
            camera.processMouseMovement(delta.x, -delta.y);
        }
    }, true);
}

CharacterController& Engine::enableCharacter(const CharacterSettings& settings, const CameraRigSettings& rig) {
    m_character = std::make_unique<CharacterController>(m_world, m_portals, m_physics, settings, rig);
    addSystem(Phase::Update, "character", [this](World&, float dt) {
        const SimInput& in = m_simInput;
        CharacterInput input;
        input.move = glm::vec2(static_cast<float>(in.held(GLFW_KEY_D)) - static_cast<float>(in.held(GLFW_KEY_A)), static_cast<float>(in.held(GLFW_KEY_W)) - static_cast<float>(in.held(GLFW_KEY_S)));
        input.jump = in.pressed(GLFW_KEY_SPACE);
        input.sprint = in.held(GLFW_KEY_LEFT_SHIFT);
        if (in.pressed(GLFW_KEY_V)) m_character->rig().toggle();
        if (in.pressed(GLFW_KEY_T)) setMouseCaptured(!m_mouseCaptured);
        if (m_mouseCaptured) m_character->look(in.current.mouseDelta.x * 0.1f, -in.current.mouseDelta.y * 0.1f);
        m_character->setInput(input);
        m_character->update(dt);
    });
    return *m_character;
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

void Engine::beginTickInput(float& dt) {
    if (m_player.active()) {
        const ReplayTick& t = m_player.next();
        m_inputPending = t.input;
        dt = t.dt;
    } else if (!m_headless && m_window) {
        InputState s;
        for (int k = 0; k <= GLFW_KEY_LAST && k < kInputKeyCount; ++k) {
            if (InputManager::IsKeyPressed(k) || InputManager::IsKeyHeld(k)) s.keys.set(static_cast<size_t>(k));
        }
        for (int b = 0; b <= GLFW_MOUSE_BUTTON_LAST && b < kInputMouseButtons; ++b) {
            if (InputManager::IsMouseButtonPressed(b) || InputManager::IsMouseButtonHeld(b)) s.buttons.set(static_cast<size_t>(b));
        }
        s.mouse = InputManager::GetMousePosition();
        s.mouseDelta = InputManager::GetMouseDelta();
        m_inputPending = s;
    }
    m_simInput.advance(m_inputPending);
    if (m_recorder.active()) m_recorder.record(dt, m_inputPending);
}

void Engine::tick(float deltaTime) {
    const auto tickStart = std::chrono::steady_clock::now();
    const double frameMs = m_lastTickStart.time_since_epoch().count() == 0 ? 0.0 : std::chrono::duration<double, std::milli>(tickStart - m_lastTickStart).count();
    if (m_streaming && frameMs > 0.0) m_streaming->recordFrame(frameMs);
    if (m_perf.active()) {
        std::vector<PerfPass> passes;
        for (const GpuPassTiming& t : m_renderer.gpuTimings()) passes.push_back({t.name, t.milliseconds});
        std::vector<PerfPass> extras;
        if (m_streaming) {
            const StreamingStats st = m_streaming->stats();
            extras = {{"frame_p99_ms", st.frameP99}, {"stream_update_ms", st.updateMillis}, {"stream_pending", static_cast<double>(st.pendingCells + st.pendingAssets)}, {"stream_resident", static_cast<double>(st.residentCells)}, {"stream_hitches", static_cast<double>(st.hitches)}, {"stream_mb", static_cast<double>(st.bytesCommitted) / 1048576.0}};
        }
        double portalMs = 0.0;
        bool hasPortalViews = false;
        for (const PortalViewStats& view : m_renderer.portalStats()) {
            if (view.depth == 0) continue;
            hasPortalViews = true;
            portalMs += view.gpuMilliseconds;
            extras.push_back({std::format("portal:{} depth {}", view.label, view.depth), view.gpuMilliseconds});
        }
        if (hasPortalViews) extras.push_back({"portal_gpu_ms", portalMs});
        m_perf.record(m_world.time().frame, frameMs, m_lastSimMs, m_renderer.gpuTotalMilliseconds(), passes, extras);
    }
    m_lastTickStart = tickStart;
    beginTickInput(deltaTime);
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
    m_lastSimMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStart).count();
}

void Engine::reloadShaders() { m_renderer.reloadShaders(); }

bool Engine::shadersChanged() {
    bool changed = false;
    std::error_code ec;
    auto check = [&](const std::filesystem::path& path) {
        const long long stamp = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
        if (ec) return;
        const std::string key = path.string();
        const auto it = m_shaderStamps.find(key);
        if (it == m_shaderStamps.end()) {
            m_shaderStamps.emplace(key, stamp);
        } else if (it->second != stamp) {
            it->second = stamp;
            changed = true;
        }
    };
    for (const auto& entry : std::filesystem::recursive_directory_iterator("resources/shaders", ec)) {
        if (entry.is_regular_file(ec)) check(entry.path());
    }
    const size_t surfaces = m_renderer.surfaceNames().size();
    for (uint32_t id = 0; id < surfaces; ++id) {
        const std::string path = m_renderer.surfacePath(id);
        if (!path.starts_with("resources/shaders")) check(path);
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
    updatePortals(m_world);
    uploadParams();
    feedGpuObservation();
    LightSet lights;
    const bool hasLights = collectLights(m_world, m_world.camera().getPosition(), lights, kMaxSceneLights);
    uploadLights(m_renderer, m_portals, hasLights, lights);
    if (m_renderer.worldOrigin() != m_world.origin()) m_reflectionFrame = 0;
    m_renderer.setWorldOrigin(m_world.origin());
    refreshReflectionScene();
    syncWindowSize(m_world.camera());
    syncInstanceBatches(m_world);
    m_renderer.drawScene(m_world.database(), m_world.camera());
    submitGpuObservation();
}

void Engine::update(World& world) {
    world.syncCamera();
    updatePortals(world);
    LightSet lights;
    const bool hasLights = collectLights(world, world.camera().getPosition(), lights, kMaxSceneLights);
    uploadLights(m_renderer, m_portals, hasLights, lights);
    m_renderer.setWorldOrigin(world.origin());
    syncWindowSize(world.camera());
    syncInstanceBatches(world);
    m_renderer.drawScene(world.database(), world.camera());
}

void Engine::updatePortals(World& world) {
    if (!m_portals.active()) {
        m_renderer.setPortalFrame({});
        return;
    }
    const int crossed = m_portals.step(world.camera());
    const EntityHandle cameraEntity = world.getActiveCamera();
    if (crossed >= 0) m_renderer.portalCrossed();
    if (crossed >= 0 && world.isAlive(cameraEntity)) world.setWorldMatrix(cameraEntity, m_portals.portals()[static_cast<size_t>(crossed)].transform * world.getWorldMatrix(cameraEntity));
    if (!m_character && m_portals.settings().uprightCamera) world.camera().relaxFrame(m_portals.cellUp(m_portals.cameraCell()), world.time().unscaledDeltaTime, m_portals.settings().uprightSeconds);
    m_portals.updateObjects(world);
    PortalFrame frame = m_portals.buildFrame(world.camera(), static_cast<float>(std::max(m_renderer.height(), 1)));
    m_visibleCells = ::visibleCells(frame);
    const std::vector<bool> owning = portalOwningCells(m_portals);
    for (PortalViewNode& node : frame.nodes) {
        if (node.cell < 0 || static_cast<size_t>(node.cell) >= owning.size() || !owning[static_cast<size_t>(node.cell)]) node.cell = -1;
    }
    m_renderer.setPortalFrame(std::move(frame));
}

void Engine::present() { m_renderer.present(); }

void Engine::syncWindowSize(Camera& camera) {
    if (!m_renderer.resizeToWindow()) return;
    m_windowWidth = m_renderer.width();
    m_windowHeight = m_renderer.height();
    camera.updateAspectRatio(static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight));
}

void Engine::cleanup() {
    stopRecording();
    m_perf.close();
    m_renderer.cleanup();
    if (m_ownsWindow && m_window) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
    m_window = nullptr;
    m_ownsWindow = false;
}
