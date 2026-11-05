module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Engine/Log/Log.hpp"
#include "Editor/Application/ApplicationInternal.hpp"

module Editor.application;

import Engine.engine;
import Engine.mesh;
import Engine.World;
import Engine.Portal;
import Engine.History;
import Engine.Physics;
import Engine.Animation;
import Engine.Render.component;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.camera;
import Engine.Overlay;
import Engine.Observation;
import Engine.TestScenes;
import Engine.Params;
import Engine.input;
import Engine.GizmoMath;
import Engine.glm;
import Editor.context;
import Editor.commands;
import Editor.viewport;
import Editor.gizmo;
import Editor.hierarchy;
import Editor.inspector;
import Editor.palette;
import Editor.hud;
import Editor.look;
import Editor.autopilot;

namespace {

constexpr size_t kDefaultObjectCount = 8000000;
constexpr size_t kDefaultMovingCount = 1500000;
constexpr int kStressMaterialCount = 16;
constexpr float kStressPositionHeight = 550.0f;
constexpr float kStressPositionSide = 230.0f;
constexpr float kStressTransparentAlpha = 0.6f;
constexpr uint32_t kPaletteSeed = 7;
constexpr float kPortalNearPlane = 0.05f;
constexpr float kHueSectors = 6.0f;

glm::vec3 hueToRgb(float hue) {
    const glm::vec3 scaled = glm::vec3(std::fmod(hue + 1.0f, 1.0f), std::fmod(hue + 2.0f / 3.0f, 1.0f), std::fmod(hue + 1.0f / 3.0f, 1.0f)) * kHueSectors;
    const glm::vec3 wrapped(std::fmod(scaled.x, kHueSectors), std::fmod(scaled.y, kHueSectors), std::fmod(scaled.z, kHueSectors));
    return glm::clamp(glm::abs(wrapped - glm::vec3(3.0f)) - glm::vec3(1.0f), glm::vec3(0.0f), glm::vec3(1.0f));
}

std::vector<uint32_t> createStressPalette(Engine& engine) {
    std::vector<uint32_t> palette;
    palette.reserve(kStressMaterialCount);
    std::mt19937 paletteRng(kPaletteSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int index = 0; index < kStressMaterialCount; ++index) {
        Material material;
        material.name = std::format("Stress {}", index);
        const float hue = static_cast<float>(index) / static_cast<float>(kStressMaterialCount);
        material.baseColor = glm::vec3(0.15f) + hueToRgb(hue) * 0.75f;
        material.metallic = index % 4 == 0 ? 1.0f : 0.0f;
        material.roughness = 0.15f + unit(paletteRng) * 0.7f;
        material.clearcoat = index % 5 == 0 ? 1.0f : 0.0f;
        palette.push_back(engine.createMaterial(material));
    }
    return palette;
}

unsigned int stressSeed() {
    const char* seed = std::getenv("LIT_SEED");
    if (seed) return static_cast<unsigned>(std::atoi(seed));
    std::random_device device;
    return device();
}

auto sceneMaterialFactory(Engine& engine, const char* label) {
    return [&engine, label](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
        Material material;
        material.name = label;
        material.baseColor = color;
        material.roughness = roughness;
        material.emissive = emissive;
        material.emissiveIntensity = intensity;
        return engine.createMaterial(material);
    };
}

}

void Application::buildStressScene() {
    World& world = m_ctx->world;
    const size_t objectCount = envSize("LIT_OBJECTS", kDefaultObjectCount);
    const size_t movingCount = std::min({envSize("LIT_MOVING", kDefaultMovingCount), objectCount, static_cast<size_t>(UINT32_MAX)});
    Lit::Log::Info("Creating {} random objects...", objectCount);
    std::mt19937 generator(stressSeed());
    std::uniform_real_distribution<float> distribHeight(-kStressPositionHeight, kStressPositionHeight);
    std::uniform_real_distribution<float> distribSide(-kStressPositionSide, kStressPositionSide);
    std::uniform_int_distribution<unsigned int> distribType(0, 1);

    const auto createStart = std::chrono::steady_clock::now();
    world.reserve(objectCount + 1);
    EntityDesc parentDesc;
    parentDesc.name = "parent";
    parentDesc.mesh = m_ctx->cubeMesh;
    m_ctx->stressParent = world.create(parentDesc);
    m_movingObjectCount = static_cast<uint32_t>(movingCount);
    m_basePositions.resize(m_movingObjectCount);
    const uint32_t cube = m_ctx->cubeMesh;
    const uint32_t sphere = m_ctx->sphereMesh;
    const EntityHandle parent = m_ctx->stressParent;
    const std::vector<uint32_t> palette = createStressPalette(m_engine);
    world.createBatch(objectCount, [&](size_t index, EntityDesc& desc) {
        desc.position = glm::vec3(distribSide(generator), distribHeight(generator), distribSide(generator));
        if (index < m_movingObjectCount) m_basePositions[index] = desc.position;
        const bool isTransparent = index % 5 == 0;
        desc.mesh = distribType(generator) == 1 ? sphere : cube;
        desc.shader = isTransparent ? 2 : (index % 7 == 0 ? 1 : 0);
        desc.material = palette[index % palette.size()];
        desc.alpha = isTransparent ? kStressTransparentAlpha : 1.0f;
        if (index < objectCount / 2) desc.parent = parent;
    });
    Lit::Log::Info("Created {} objects in {:.1f} ms", objectCount, elapsedMilliseconds(createStart));
    m_engine.uploadBasePositions(m_basePositions);
}

std::vector<std::string> Application::sceneFiles() const {
    std::vector<std::string> files;
    std::error_code directoryError;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("resources", directoryError)) {
        if (entry.path().extension() == ".litscene") files.push_back(entry.path().generic_string());
    }
    std::ranges::sort(files);
    return files;
}

void Application::buildPortalScene(int which) {
    EditorContext& ctx = *m_ctx;
    PortalSceneKit kit;
    kit.cube = ctx.cubeMesh;
    kit.sphere = ctx.sphereMesh;
    if (const std::optional<uint32_t> floorShader = findNameIndex(m_engine.shaderNames(), "Checker")) kit.floorShader = *floorShader;
    kit.material = sceneMaterialFactory(m_engine, "Portal scene");
    const PortalTestScene scene = which == 1 ? PortalTestScene::Torture : PortalTestScene::Loop;
    ::buildPortalScene(m_engine.portals(), ctx.world, kit, scene);
    float yaw = 0.0f;
    float pitch = 0.0f;
    const glm::vec3 start = portalSceneStart(scene, yaw, pitch);
    Camera& camera = ctx.world.camera();
    camera.setPos(start);
    camera.setOrientation(yaw, pitch);
    camera.setNearPlane(kPortalNearPlane);
    m_engine.portals().setCameraCell(m_engine.portals().cellAt(start));
}

bool Application::buildTestScene(const std::string& name) {
    EditorContext& ctx = *m_ctx;
    const std::vector<std::string> names = testSceneNames();
    if (std::ranges::find(names, name) == names.end()) return false;
    if (ctx.playing) setPlaying(false);
    ctx.world.clear();
    m_engine.portals().clear();
    m_engine.setAnimation(0.0f, 0, 1);
    TestSceneKit kit;
    kit.cube = ctx.cubeMesh;
    kit.sphere = ctx.sphereMesh;
    if (const std::optional<uint32_t> floorShader = findNameIndex(m_engine.shaderNames(), "Checker")) kit.floorShader = *floorShader;
    kit.material = sceneMaterialFactory(m_engine, "Test scene");
    TestSceneInfo info;
    if (!::buildTestScene(m_engine, name, kit, info)) return false;
    applyTestSceneCamera(m_engine, info);
    afterSceneReplaced();
    return true;
}

void Application::saveScene(const std::string& path) {
    const auto start = std::chrono::steady_clock::now();
    if (!m_ctx->world.saveScene(path)) {
        m_ctx->toast("Could not save " + path, ToastKind::Error);
        return;
    }
    m_ctx->scenePath = path;
    m_hotReload.noteSaved(path);
    m_ctx->savedRevision = m_ctx->history->state();
    m_ctx->toast(std::format("Saved {} ({:.0f} ms)", path, elapsedMilliseconds(start)), ToastKind::Success);
}

void Application::afterSceneReplaced() {
    m_observation = std::make_unique<ObservationSystem>(m_ctx->world);
    m_overlay.attachObservation(m_observation.get());
    m_ctx->selection.clear();
    m_ctx->history->clear();
    m_ctx->savedRevision = m_ctx->history->state();
    m_ctx->stressParent = m_ctx->world.find("parent");
    m_viewport.cancelInteractions(*m_ctx);
    m_stableSettings = m_engine.renderSettings();
}

void Application::loadScene(const std::string& path) {
    if (m_ctx->playing) setPlaying(false);
    const auto start = std::chrono::steady_clock::now();
    m_engine.preloadSceneMeshes(path);
    if (!m_ctx->world.loadScene(path)) {
        m_ctx->toast("Could not open " + path, ToastKind::Error);
        return;
    }
    m_engine.portals().clear();
    m_ctx->scenePath = path;
    afterSceneReplaced();
    m_ctx->toast(std::format("Opened {} ({:.0f} ms)", path, elapsedMilliseconds(start)), ToastKind::Success);
}

void Application::replaceSceneWith(const std::function<void()>& build, const char* message) {
    if (m_ctx->playing) setPlaying(false);
    m_ctx->world.clear();
    m_engine.setAnimation(0.0f, 0, 1);
    build();
    afterSceneReplaced();
    m_ctx->toast(message, ToastKind::Success);
}

void Application::setPlaying(bool playing) {
    EditorContext& ctx = *m_ctx;
    if (playing == ctx.playing) return;
    const auto start = std::chrono::steady_clock::now();
    if (playing) {
        ctx.playSnapshot = ctx.world.captureSnapshot();
        m_playSelection = ctx.selection.items();
        m_editHistory = std::move(ctx.history);
        ctx.history = std::make_unique<History>(ctx.world);
        ctx.paused = false;
        m_engine.setPaused(false);
        ctx.toast(std::format("Playing ({:.0f} ms). F5 stops and restores the scene", elapsedMilliseconds(start)), ToastKind::Success);
    } else {
        m_engine.setPaused(true);
        ctx.world.restoreSnapshot(ctx.playSnapshot);
        const std::vector<EntityHandle> kept = std::move(m_playSelection);
        m_playSelection.clear();
        ctx.playSnapshot = WorldSnapshot{};
        m_stableSettings = m_engine.renderSettings();
        if (m_editHistory) ctx.history = std::move(m_editHistory);
        ctx.selection.clear();
        for (EntityHandle entity : kept) {
            if (ctx.world.isAlive(entity)) ctx.selection.add(entity);
        }
        ctx.stressParent = ctx.world.find("parent");
        ctx.paused = false;
        ctx.toast(std::format("Stopped, scene restored ({:.0f} ms)", elapsedMilliseconds(start)));
    }
    ctx.playing = playing;
}
