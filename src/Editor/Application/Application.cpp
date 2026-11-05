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

constexpr const char* kWindowTitle = "Lit Editor";
constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 900;
constexpr float kEditorFarPlane = 2000.0f;
constexpr float kMaxFrameSeconds = 0.25f;
constexpr float kSteppedFrameSeconds = 1.0f / 60.0f;
constexpr double kAutosaveIdleSeconds = 3.0;
constexpr float kDefaultCameraYaw = -90.0f;
constexpr int kShaderErrorLineLimit = 3;
const glm::vec3 kStressCameraPosition(200.0f, 1000.0f, 200.0f);
constexpr float kStressCameraYaw = -135.0f;
constexpr float kStressCameraPitch = -74.2f;
constexpr const char* kPostFxPath = "resources/data/post_fx.json";
constexpr const char* kParamsPath = "resources/data/params.json";

}

Application::Application() {
    if (!m_engine.open({kWindowTitle, kWindowWidth, kWindowHeight})) return;
    m_window = m_engine.window();
    m_engine.enableImGui();
    Lit::Log::Phase("imgui");
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(m_window, &scaleX, &scaleY);
    applyEditorTheme(std::max(scaleX, scaleY));
    m_ctx = std::make_unique<EditorContext>(m_engine);

    std::filesystem::create_directories("resources/models");
    std::filesystem::create_directories("resources/assets");
    m_ctx->cubeMesh = m_engine.loadMesh("cube");
    m_ctx->sphereMesh = m_engine.loadMesh("sphere");
    Lit::Log::Phase("editor context and base meshes");

    registerPhysicsComponents(m_ctx->world);
    registerAnimationComponents(m_ctx->world);
    buildInitialScene();
    m_engine.setFullProfiling(true);
    m_engine.setProfilingHud(false);
    m_engine.setPaused(true);
    m_engine.setShaderWatch(true);
    if (std::filesystem::exists(kParamsPath)) m_engine.params().loadFile(kParamsPath);
    if (std::filesystem::exists(kPostFxPath)) m_engine.loadPostFx(kPostFxPath);
    m_observation = std::make_unique<ObservationSystem>(m_ctx->world);
    m_overlay.attachObservation(m_observation.get());
    applyEnvironmentOverrides();
    setupDefaultEffects();
    registerCommands();
    m_hud.setPieItems({{"Move", "transform.move"}, {"Rotate", "transform.rotate"}, {"Scale", "transform.scale"}, {"Duplicate", "edit.duplicate"}, {"Delete", "edit.delete"}, {"Focus", "view.focus"}, {"Add...", "create.menu"}, {"Hide", "hide.selected"}},
                       {{"Add...", "create.menu"}, {"Select all", "select.all"}, {"Paste", "edit.paste"}, {"Frame all", "view.frameAll"}, {"World", "view.world"}, {"Ortho", "view.ortho"}, {"Reveal all", "hide.revealAll"}, {"Look", "view.look"}});
    m_ctx->savedRevision = m_ctx->history->state();
    m_stableSettings = m_engine.renderSettings();
    m_lastFrame = glfwGetTime();
    if (const char* script = std::getenv("LIT_AUTOPILOT")) setupAutopilot(script);
    Lit::Log::Phase("editor ready");
}

Application::~Application() {
    std::error_code removeError;
    if (m_ctx && (!m_ctx->dirty() || m_discardConfirmed)) std::filesystem::remove(kAutosavePath, removeError);
    m_ctx.reset();
    m_engine.cleanup();
}

bool Application::isRunning() const { return m_window && m_ctx && !m_ctx->requestQuit; }

void Application::buildInitialScene() {
    const char* sceneChoice = std::getenv("LIT_SCENE");
    const std::string_view sceneName = sceneChoice ? sceneChoice : "";
    const bool showcase = sceneName == "showcase";
    const bool portalLoop = sceneName == "portal_loop";
    const bool portalTorture = sceneName == "portal_torture";
    Camera& camera = m_ctx->world.camera();
    camera.setFarPlane(kEditorFarPlane);
    const std::vector<std::string> testScenes = testSceneNames();
    const bool testScene = sceneChoice && !portalTorture && std::ranges::find(testScenes, std::string(sceneName)) != testScenes.end();
    if (testScene) {
        buildTestScene(sceneChoice);
    } else if (portalLoop || portalTorture) {
        buildPortalScene(portalTorture ? 1 : 0);
    } else if (showcase) {
        buildShowcaseScene();
    } else {
        buildStressScene();
        camera.setPos(kStressCameraPosition);
        camera.setOrientation(kStressCameraYaw, kStressCameraPitch);
    }
    Lit::Log::Phase(showcase ? "showcase scene" : "stress scene");
}

void Application::applyEnvironmentOverrides() {
    if (const char* overlay = std::getenv("LIT_OVERLAY")) {
        m_overlay.setVisible(true);
        m_overlay.setTab(std::max(0, std::atoi(overlay) - 1));
    }
    if (const char* interval = std::getenv("LIT_AUTOSAVE_SECONDS")) m_autosaveInterval = static_cast<float>(std::atof(interval));
    m_recoveryPending = std::filesystem::exists(kAutosavePath);
    if (std::getenv("LIT_PROFILE")) m_engine.profiler().setEnabled(true);

    Camera& camera = m_ctx->world.camera();
    if (const char* cameraPosition = std::getenv("LIT_CAM_POS")) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (std::sscanf(cameraPosition, "%f,%f,%f", &x, &y, &z) == 3) camera.setPos(glm::vec3(x, y, z));
    }
    if (const char* cameraPitch = std::getenv("LIT_CAM_PITCH")) {
        const char* cameraYaw = std::getenv("LIT_CAM_YAW");
        camera.setOrientation(cameraYaw ? static_cast<float>(std::atof(cameraYaw)) : kDefaultCameraYaw, static_cast<float>(std::atof(cameraPitch)));
    }
    if (const char* forcedLod = std::getenv("LIT_FORCE_LOD")) m_lookState.forcedLod = std::atoi(forcedLod);
    if (const char* lodBias = std::getenv("LIT_LOD_BIAS")) m_lookState.lodBias = static_cast<float>(std::atof(lodBias));
    m_engine.setForcedLod(m_lookState.forcedLod);
    m_engine.setLodBias(m_lookState.lodBias);
}

void Application::setupDefaultEffects() {
    m_engine.loadShaderTexture("u_lut", "resources/textures/grade_warm.png", false);
    for (const char* effect : {"outline", "sharpen", "chromatic", "underwater", "grade"}) {
        const uint32_t id = m_engine.addEffect(effect);
        m_engine.setPassEnabled(PassKind::Effect, id, false);
    }
}

void Application::guarded(std::function<void()> action) {
    if (!m_ctx->dirty()) {
        action();
        return;
    }
    m_pendingAction = std::move(action);
    m_unsavedPromptOpen = true;
}

void Application::updateAttention(float dt) {
    if (!m_observation) return;
    World& world = m_ctx->world;
    const glm::vec2 size = m_engine.windowSize();
    m_observation->setView(observationViewFromCamera(world.camera(), size.y > 0.0f ? size.x / size.y : 1.0f));
    const std::vector<EntityHandle> observed = world.findByTag("observed");
    for (EntityHandle entity : observed) {
        if (!m_observation->tracked(entity)) m_observation->track(entity);
    }
    m_observation->update(dt);
    for (EntityHandle entity : observed) {
        const bool attended = m_observation->state(entity) == ObservationState::Attended;
        if (((world.getRenderFlags(entity) & RENDER_MASKED) != 0) != attended) world.setRenderFlags(entity, RENDER_MASKED, attended);
    }
}

void Application::reportShaderErrors() {
    for (const ShaderError& error : m_engine.takeShaderErrors()) {
        const std::string file = std::filesystem::path(error.source).filename().string();
        std::string message = error.message;
        if (!error.source.empty()) {
            for (size_t at = 0; (at = message.find(error.source, at)) != std::string::npos; at += file.size()) message.replace(at, error.source.size(), file);
        }
        size_t cut = 0;
        for (int line = 0; line < kShaderErrorLineLimit && cut != std::string::npos; ++line) cut = message.find('\n', cut == 0 ? 0 : cut + 1);
        m_hotReload.reportError(ReloadKind::Shader, error.source, message);
        m_ctx->toast(std::format("Shader error in {}\n{}", file, message.substr(0, cut)), ToastKind::Error);
    }
}

void Application::processInput(float deltaTime) {
    EditorContext& ctx = *m_ctx;
    const ImGuiIO& io = ImGui::GetIO();
    const bool paletteOpen = m_palette.isOpen();
    const bool blocking = paletteOpen || ctx.showHelp || m_recoveryPending || m_unsavedPromptOpen;
    const bool mouseFree = !io.WantCaptureMouse && !blocking;
    m_viewport.update(ctx, deltaTime, mouseFree);
    const bool pieBusy = m_hud.updatePie(ctx, m_commands, !blocking && !ctx.modalActive && !ctx.flying && !io.WantTextInput);
    const bool modalBefore = ctx.modalActive;
    m_tool.update(ctx);
    if (!modalBefore && !ctx.modalActive && !ctx.flying && !pieBusy && !blocking) {
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        m_commands.processShortcuts(!popupOpen);
    } else if (ctx.showHelp && !paletteOpen && (ImGui::IsKeyPressed(ImGuiKey_F1) || ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        ctx.showHelp = false;
    }
    ImGuizmo::Enable(mouseFree && !ctx.modalActive);
    m_tool.drawHandles(ctx);
    m_viewport.drawOverlays(ctx);

    ctx.selection.prune(ctx.world);
    if (ctx.primary() != m_lastPrimary) {
        m_lastPrimary = ctx.primary();
        if (!m_lastPrimary.isNull()) m_outliner.reveal(ctx, m_lastPrimary);
    }
}

void Application::drawPanels(float deltaTime) {
    EditorContext& ctx = *m_ctx;
    m_outliner.draw(ctx);
    m_look.draw(ctx, m_lookState);
    m_look.drawWorld(ctx);
    m_inspector.draw(ctx);
    drawStats(m_frameMs);
    updateAttention(deltaTime);
    m_overlay.draw(m_engine);
    m_profilerPanel.draw(ctx);
    m_diagnostics.draw(ctx);
    m_hotReload.draw(ctx);
    m_hud.drawTimeline(ctx);
    m_hud.drawStatusBar(ctx, m_commands, m_frameMs);
    drawQuickAdd();
    drawRecovery();
    drawUnsavedPrompt();
    m_hud.drawHelp(ctx, m_commands);
    m_palette.draw(ctx, m_commands);
    m_hud.drawToasts(ctx, deltaTime);
}

void Application::tickEngine(float deltaTime) {
    if (m_stepOnce && m_ctx->playing && m_ctx->paused) {
        m_engine.setPaused(false);
        m_engine.tick(kSteppedFrameSeconds);
        m_engine.setPaused(true);
        m_stepOnce = false;
        return;
    }
    m_engine.tick(deltaTime);
}

void Application::updateInputIdleTime(double now) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool pointerActive = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || ImGui::IsAnyMouseDown();
    const bool keyboardActive = io.InputQueueCharacters.Size > 0 || ImGui::IsKeyDown(ImGuiKey_Space) || io.KeyCtrl || io.KeyShift || io.KeyAlt;
    if (pointerActive || keyboardActive) {
        m_lastInputTime = now;
        return;
    }
    for (int keyIndex = ImGuiKey_NamedKey_BEGIN; keyIndex < ImGuiKey_NamedKey_END; ++keyIndex) {
        if (!ImGui::IsKeyDown(static_cast<ImGuiKey>(keyIndex))) continue;
        m_lastInputTime = now;
        return;
    }
}

void Application::updateAutosave(float deltaTime, double now) {
    if (m_ctx->playing || m_autosaveInterval <= 0.0f) return;
    m_autosaveTimer += deltaTime;
    if (m_autosaveTimer < m_autosaveInterval || now - m_lastInputTime < kAutosaveIdleSeconds) return;
    m_autosaveTimer = 0.0f;
    if (m_ctx->dirty() && m_ctx->world.saveScene(kAutosavePath)) m_ctx->toast("Autosaved");
}

void Application::reportOverCapacity() {
    const size_t over = m_engine.renderOverCapacity();
    if (over == m_reportedOverCapacity) return;
    m_reportedOverCapacity = over;
    if (over > 0) m_ctx->toast(std::format("{} entities exceed GPU memory; the viewport is paused. Undo (Ctrl+Z) or delete entities to resume.", over), ToastKind::Error);
}

void Application::update() {
    EditorContext& ctx = *m_ctx;
    if (glfwWindowShouldClose(m_window)) {
        glfwSetWindowShouldClose(m_window, GLFW_FALSE);
        guarded([this] { m_ctx->requestQuit = true; });
    }
    const double now = glfwGetTime();
    const float deltaTime = static_cast<float>(std::min(now - m_lastFrame, static_cast<double>(kMaxFrameSeconds)));
    m_lastFrame = now;
    m_frameMs = deltaTime * 1000.0f;

    m_autopilot.beforeFrame(ctx, m_commands, deltaTime);
    reportShaderErrors();
    updateTools(deltaTime);
    m_engine.beginUi();
    ctx.hints.clear();
    ctx.modeLabel = "EDIT";
    ctx.panels.clear();
    const float statsHeight = ctx.showStats && !ctx.zen ? static_cast<float>(kStatsLineCount) * (ImGui::GetTextLineHeight() + kStatsLineSpacing) + kStatsPadding : 0.0f;
    ctx.rightColumnTop = ImGui::GetMainViewport()->WorkPos.y + (ctx.zen ? kZenTopOffset : kStatsTopOffset) + statsHeight;

    processInput(deltaTime);
    drawPanels(deltaTime);
    tickEngine(deltaTime);
    if (m_lookState.animateStress) m_engine.setAnimation(static_cast<float>(now), m_movingObjectCount, 1);

    updateInputIdleTime(now);
    updateAutosave(deltaTime, now);
    reportOverCapacity();
    trackRenderSettings();
    updateTitle();
    m_engine.update();
    InputManager::Update();
    m_engine.present();
    if (m_framesPresented++ == 0) Lit::Log::Phase("first frame presented");
    glfwPollEvents();
}
