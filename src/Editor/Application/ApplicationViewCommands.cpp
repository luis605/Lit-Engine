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

constexpr float kFlySpeedFactor = 1.5f;
constexpr float kMaxFlySpeed = 5000.0f;
constexpr float kMinFlySpeed = 0.1f;
constexpr int kHandleModeCount = 4;

struct Nudge {
    ImGuiKey key;
    glm::vec3 direction;
    const char* name;
};

const Nudge kNudges[] = {
    {ImGuiKey_LeftArrow, glm::vec3(-1, 0, 0), "left"},
    {ImGuiKey_RightArrow, glm::vec3(1, 0, 0), "right"},
    {ImGuiKey_UpArrow, glm::vec3(0, 0, 1), "away"},
    {ImGuiKey_DownArrow, glm::vec3(0, 0, -1), "closer"},
    {ImGuiKey_PageUp, glm::vec3(0, 1, 0), "up"},
    {ImGuiKey_PageDown, glm::vec3(0, -1, 0), "down"},
};

const char* const kHandleModeNames[kHandleModeCount] = {"Move handles", "Rotate handles", "Scale handles", "Handles off"};

std::string primitiveCommandId(const std::string& kind) {
    std::string id = "create.";
    for (char character : kind) {
        if (character != ' ') id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return id;
}

}

void Application::registerTransformCommands() {
    addCommand("transform.move", "Move (type X/Y/Z and a value)", "Transform", {key(ImGuiKey_G)}, [this] { m_tool.begin(*m_ctx, ModalKind::Move); }, selectionRequired());
    addCommand("transform.rotate", "Rotate", "Transform", {key(ImGuiKey_R)}, [this] { m_tool.begin(*m_ctx, ModalKind::Rotate); }, selectionRequired());
    addCommand("transform.scale", "Scale", "Transform", {key(ImGuiKey_S)}, [this] { m_tool.begin(*m_ctx, ModalKind::Scale); }, selectionRequired());
    addCommand("transform.handles", "Cycle handles: move / rotate / scale / off", "Transform", {key(ImGuiKey_Q)}, [this] {
        m_ctx->handles = static_cast<HandleMode>((static_cast<int>(m_ctx->handles) + 1) % kHandleModeCount);
        m_ctx->toast(kHandleModeNames[static_cast<int>(m_ctx->handles)]);
    });
    addCommand("transform.space", "Toggle local / world space", "Transform", {key(ImGuiKey_GraveAccent)}, [this] {
        m_ctx->localSpace = !m_ctx->localSpace;
        m_ctx->toast(m_ctx->localSpace ? "Local space" : "World space");
    }).checked = [this] { return m_ctx->localSpace; };
    addCommand("transform.snap", "Toggle snapping", "Transform", {shift(ImGuiKey_Tab)}, [this] {
        m_ctx->snapping = !m_ctx->snapping;
        m_ctx->toast(m_ctx->snapping ? std::format("Snapping on ({} m, {}°)", m_ctx->snapMove, m_ctx->snapAngle) : "Snapping off");
    }).checked = [this] { return m_ctx->snapping; };
    addCommand("transform.resetPosition", "Reset position", "Transform", {alt(ImGuiKey_G)}, [this] { m_tool.resetTransform(*m_ctx, 0); }, selectionRequired());
    addCommand("transform.resetRotation", "Reset rotation", "Transform", {alt(ImGuiKey_R)}, [this] { m_tool.resetTransform(*m_ctx, 1); }, selectionRequired());
    addCommand("transform.resetScale", "Reset scale", "Transform", {alt(ImGuiKey_S)}, [this] { m_tool.resetTransform(*m_ctx, 2); }, selectionRequired());
    addCommand("transform.toCursor", "Move selection to cursor", "Transform", {key(ImGuiKey_T)}, [this] { moveSelectionToCursor(); }, selectionRequired());
    addCommand("transform.drop", "Drop to ground", "Transform", {key(ImGuiKey_End)}, [this] { dropSelectionToGround(); }, selectionRequired());
}

void Application::registerNudgeCommands() {
    for (const Nudge& nudge : kNudges) {
        const glm::vec3 direction = nudge.direction;
        Command& command = addCommand(std::format("transform.nudge.{}", nudge.name), std::format("Nudge {} (Shift x10)", nudge.name), "Transform", {key(nudge.key), shift(nudge.key)}, [this, direction] { m_tool.nudge(*m_ctx, direction); }, selectionRequired());
        command.repeat = true;
        command.palette = false;
        command.enabled = [this] { return !m_ctx->selection.empty() && !m_ctx->outlinerFocused; };
        command.help = nudge.key == ImGuiKey_LeftArrow;
        if (command.help) {
            command.title = "Nudge along the ground (PgUp/PgDn vertical, Shift x10)";
            command.keysLabel = "Arrows";
        }
    }
}

void Application::registerViewCommands() {
    addCommand("view.focus", "Frame selection", "View", {key(ImGuiKey_F)}, [this] { frameSelection(); });
    addCommand("view.frameAll", "Frame everything", "View", {key(ImGuiKey_Home), shift(ImGuiKey_F)}, [this] { frameAll(); });
    addCommand("view.top", "View from top", "View", {key(ImGuiKey_Keypad7)}, [this] { setView(glm::vec3(0.0f, -1.0f, 0.0f)); });
    addCommand("view.bottom", "View from bottom", "View", {ctrl(ImGuiKey_Keypad7)}, [this] { setView(glm::vec3(0.0f, 1.0f, 0.0f)); });
    addCommand("view.front", "View from front", "View", {key(ImGuiKey_Keypad1)}, [this] { setView(glm::vec3(0.0f, 0.0f, -1.0f)); });
    addCommand("view.back", "View from back", "View", {ctrl(ImGuiKey_Keypad1)}, [this] { setView(glm::vec3(0.0f, 0.0f, 1.0f)); });
    addCommand("view.ortho", "Toggle orthographic / perspective", "View", {key(ImGuiKey_Keypad5)}, [this] { toggleOrthographic(); }).checked = [this] { return m_ctx->world.camera().isOrthographic(); };
    addCommand("view.right", "View from right", "View", {key(ImGuiKey_Keypad3)}, [this] { setView(glm::vec3(-1.0f, 0.0f, 0.0f)); });
    addCommand("view.left", "View from left", "View", {ctrl(ImGuiKey_Keypad3)}, [this] { setView(glm::vec3(1.0f, 0.0f, 0.0f)); });
}

void Application::registerPanelCommands() {
    addCommand("view.zen", "Zen mode (hide all panels)", "View", {key(ImGuiKey_Tab)}, [this] { m_ctx->zen = !m_ctx->zen; }).checked = [this] { return m_ctx->zen; };
    addCommand("view.outliner", "Scene outliner", "View", {key(ImGuiKey_O)}, [this] { m_ctx->showOutliner = !m_ctx->showOutliner; }).checked = [this] { return m_ctx->showOutliner; };
    addCommand("view.search", "Filter the outliner", "View", {ctrlShift(ImGuiKey_F)}, [this] {
        m_ctx->showOutliner = true;
        m_outliner.focusSearch();
    }, {}, CommandScope::Global);
    addCommand("view.inspector", "Inspector card", "View", {key(ImGuiKey_I)}, [this] { m_ctx->showInspector = !m_ctx->showInspector; }).checked = [this] { return m_ctx->showInspector; };
    addCommand("view.pinInspector", "Pin / float the inspector card", "View", {ctrlShift(ImGuiKey_I)}, [this] {
        m_ctx->pinInspector = !m_ctx->pinInspector;
        if (m_ctx->pinInspector) m_inspector.dock();
        m_ctx->toast(m_ctx->pinInspector ? "Card pinned" : "Card follows the selection");
    }, {}, CommandScope::Global).checked = [this] { return m_ctx->pinInspector; };
    addCommand("view.world", "World: sky, sun, exposure, fog", "View", {shift(ImGuiKey_W)}, [this] {
        const bool show = !m_look.worldVisible();
        m_ctx->showWorld = show && !m_ctx->selection.empty();
        m_ctx->worldDismissed = !show;
        if (show && m_ctx->showLook && m_ctx->selection.empty()) m_ctx->showWorld = true;
    }).checked = [this] { return m_look.worldVisible(); };
    addCommand("view.look", "Look: rendering and lighting", "View", {key(ImGuiKey_L)}, [this] { m_ctx->showLook = !m_ctx->showLook; }).checked = [this] { return m_ctx->showLook; };
    addCommand("view.stats", "Stats overlay", "View", {ctrlShift(ImGuiKey_T)}, [this] { m_ctx->showStats = !m_ctx->showStats; }, {}, CommandScope::Global).checked = [this] { return m_ctx->showStats; };
    addCommand("view.grid", "Grid", "View", {ctrlShift(ImGuiKey_G)}, [this] { m_ctx->showGrid = !m_ctx->showGrid; }).checked = [this] { return m_ctx->showGrid; };
    addCommand("view.timeline", "History timeline", "View", {ctrl(ImGuiKey_H)}, [this] { m_ctx->showTimeline = !m_ctx->showTimeline; }, {}, CommandScope::Global).checked = [this] { return m_ctx->showTimeline; };
    addCommand("view.help", "Keyboard map", "View", {key(ImGuiKey_F1), shift(ImGuiKey_Slash)}, [this] { m_ctx->showHelp = !m_ctx->showHelp; }, {}, CommandScope::Global);
    addCommand("view.palette", "Command palette", "View", {ctrl(ImGuiKey_P), ctrlShift(ImGuiKey_P)}, [this] {
        m_ctx->openPalette = true;
        m_ctx->paletteSeed = ImGui::GetIO().KeyShift ? ">" : "";
    }, {}, CommandScope::Global).palette = false;
    addCommand("view.calculator", "Calculator", "View", {key(ImGuiKey_Equal)}, [this] {
        m_ctx->openPalette = true;
        m_ctx->paletteSeed = "=";
    });
    addCommand("view.flyFaster", "Faster camera", "View", {key(ImGuiKey_KeypadAdd)}, [this] { m_ctx->flySpeed = std::min(m_ctx->flySpeed * kFlySpeedFactor, kMaxFlySpeed); });
    addCommand("view.flySlower", "Slower camera", "View", {key(ImGuiKey_KeypadSubtract)}, [this] { m_ctx->flySpeed = std::max(m_ctx->flySpeed / kFlySpeedFactor, kMinFlySpeed); });
}

void Application::registerCreateCommands() {
    addCommand("create.menu", "Add...", "Create", {shift(ImGuiKey_A)}, [this] { m_ctx->openQuickAdd = true; });
    for (const char* kind : {"Cube", "Sphere", "Empty", "Point light", "Spot light", "Sun light", "Camera"}) {
        const std::string name = kind;
        addCommand(primitiveCommandId(name), "Add " + name, "Create", {}, [this, name] { createPrimitive(name); });
    }
}
