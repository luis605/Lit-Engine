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

constexpr float kPromptButtonWidth = 140.0f;
constexpr float kRecoveryButtonWidth = 150.0f;
constexpr float kStatsRightMargin = 16.0f;
constexpr ImU32 kStatsTextColor = IM_COL32(200, 205, 214, 200);
constexpr float kMinFrameMilliseconds = 0.01f;

struct QuickAddAccelerator {
    ImGuiKey key;
    const char* kind;
};

constexpr QuickAddAccelerator kQuickAddAccelerators[] = {{ImGuiKey_C, "Cube"}, {ImGuiKey_S, "Sphere"}, {ImGuiKey_E, "Empty"}, {ImGuiKey_P, "Point light"}, {ImGuiKey_O, "Spot light"}, {ImGuiKey_U, "Sun light"}, {ImGuiKey_A, "Camera"}};

}

void Application::drawUnsavedPrompt() {
    if (!m_unsavedPromptOpen) return;
    ImGui::OpenPopup("Unsaved changes");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%s has unsaved changes.", m_ctx->scenePath.c_str());
    ImGui::Spacing();
    auto finish = [&](bool run) {
        m_unsavedPromptOpen = false;
        ImGui::CloseCurrentPopup();
        if (run && m_pendingAction) m_pendingAction();
        m_pendingAction = {};
    };
    const ImVec2 buttonSize(kPromptButtonWidth, 0.0f);
    if (ImGui::Button("Save  (Enter)", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        saveScene(m_ctx->scenePath);
        finish(!m_ctx->dirty());
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't save  (D)", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        m_discardConfirmed = true;
        finish(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel  (Esc)", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) finish(false);
    ImGui::EndPopup();
}

void Application::trackRenderSettings() {
    if (ImGui::IsAnyItemActive()) return;
    const RenderSettings current = m_engine.renderSettings();
    RenderSettings stable = m_stableSettings;
    if (current.animateTime) stable.timeOfDay = current.timeOfDay;
    if (current == stable) return;
    const RenderSettings before = m_stableSettings;
    m_stableSettings = current;
    m_ctx->history->record("Render settings", [this, current] { m_engine.renderSettings() = m_stableSettings = current; }, [this, before] { m_engine.renderSettings() = m_stableSettings = before; });
}

void Application::drawQuickAdd() {
    EditorContext& ctx = *m_ctx;
    if (ctx.openQuickAdd) {
        ImGui::OpenPopup("Add");
        ctx.openQuickAdd = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetIO().MousePos, ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("Add")) return;
    ImGui::TextDisabled(ctx.cursorOnSurface ? "Add on the surface under the cursor" : "Add at the cursor");
    ImGui::Separator();
    auto item = [&](const char* label, const char* keyHint) {
        if (ImGui::MenuItem(label, keyHint)) createPrimitive(label);
    };
    ImGui::SeparatorText("Shapes");
    item("Cube", "C");
    item("Sphere", "S");
    for (const std::string& mesh : m_engine.availableMeshes()) {
        if (mesh == "cube" || mesh == "sphere") continue;
        if (ImGui::MenuItem(mesh.c_str())) createPrimitive(mesh);
    }
    item("Empty", "E");
    ImGui::SeparatorText("Lights");
    item("Point light", "P");
    item("Spot light", "O");
    item("Sun light", "U");
    ImGui::SeparatorText("Other");
    item("Camera", "A");
    const ImGuiIO& io = ImGui::GetIO();
    const bool acceptKeys = !ImGui::IsWindowAppearing() && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
    for (const QuickAddAccelerator& accelerator : kQuickAddAccelerators) {
        if (!acceptKeys || !ImGui::IsKeyPressed(accelerator.key, false)) continue;
        createPrimitive(accelerator.kind);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Application::drawRecovery() {
    if (!m_recoveryPending) return;
    ImGui::OpenPopup("Recover?");
    if (!ImGui::BeginPopupModal("Recover?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("An autosave from a previous session was found.");
    ImGui::TextDisabled("%s", kAutosavePath);
    ImGui::Spacing();
    const ImVec2 buttonSize(kRecoveryButtonWidth, 0.0f);
    if (ImGui::Button("Recover  (Y)", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Y)) {
        loadScene(kAutosavePath);
        m_recoveryPending = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard  (N)", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_N) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        std::error_code removeError;
        std::filesystem::remove(kAutosavePath, removeError);
        m_recoveryPending = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Application::drawStats(float frameMs) {
    EditorContext& ctx = *m_ctx;
    if (!ctx.showStats || ctx.zen) return;
    const glm::vec2 size = m_engine.windowSize();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const Camera& camera = ctx.world.camera();
    const glm::vec3 position = camera.getPosition();
    size_t lights = 0;
    ctx.world.view<LightComponent>([&lights](EntityHandle, LightComponent&) { ++lights; });
    const std::string lines[kStatsLineCount] = {
        std::format("{:.1f} fps  {:.2f} ms", 1000.0f / std::max(frameMs, kMinFrameMilliseconds), frameMs),
        std::format("gpu {:.2f} ms", m_engine.gpuTotalMilliseconds()),
        std::format("{} entities  {} lights  {} selected", ctx.world.aliveCount(), lights, ctx.selection.size()),
        std::format("cam {:.0f} {:.0f} {:.0f}  {}", position.x, position.y, position.z, camera.isOrthographic() ? "ortho" : "persp"),
        std::format("speed {:.1f}  {}{}", ctx.flySpeed, ctx.snapping ? "snap  " : "", ctx.localSpace ? "local" : "world"),
    };
    float lineY = ImGui::GetMainViewport()->WorkPos.y + kStatsTopOffset;
    for (const std::string& line : lines) {
        const ImVec2 textSize = ImGui::CalcTextSize(line.c_str());
        draw->AddText(ImVec2(size.x - textSize.x - kStatsRightMargin, lineY), kStatsTextColor, line.c_str());
        lineY += textSize.y + kStatsLineSpacing;
    }
}

void Application::updateTitle() {
    const std::string title = std::format("Lit Editor  ·  {}{}{}", m_ctx->scenePath, m_ctx->dirty() ? " *" : "", m_ctx->playing ? "  [playing]" : "");
    if (title == m_lastTitle) return;
    glfwSetWindowTitle(m_window, title.c_str());
    m_lastTitle = title;
}
