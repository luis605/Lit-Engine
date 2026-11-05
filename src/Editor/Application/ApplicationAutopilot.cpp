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

std::string flag(bool value) { return value ? "1" : "0"; }

std::string formatFloat(float value) { return std::format("{:.2f}", value); }

}

void Application::setupAutopilot(const char* script) {
    m_recoveryPending = false;
    m_autopilot.load(script);
    m_autopilot.window = m_window;
    m_autopilot.loadTestScene = [this](const std::string& name) { return buildTestScene(name); };
    m_autopilot.showOverlay = [this](int tab) {
        m_overlay.setVisible(tab >= 0);
        if (tab > 0) m_overlay.setTab(tab - 1);
    };
    m_autopilot.saveScene = [this](const std::string& path) { saveScene(path); };
    m_autopilot.loadScene = [this](const std::string& path) { loadScene(path); };
    m_autopilot.editMaterial = [this](const std::string& field, float value) { return autopilotEditMaterial(field, value); };
    m_autopilot.query = [this](const std::string& what) { return autopilotQuery(what); };
}

bool Application::autopilotEditMaterial(const std::string& field, float value) {
    const EntityHandle primary = m_ctx->primary();
    if (!m_ctx->world.isAlive(primary)) return false;
    const uint32_t id = m_ctx->world.getRenderable(primary).material_uuid;
    Material material = m_engine.material(id);
    if (field == "roughness") material.roughness = value;
    else if (field == "metallic") material.metallic = value;
    else if (field == "opacity") material.opacity = value;
    else if (field == "clearcoat") material.clearcoat = value;
    else return false;
    m_engine.setMaterial(id, material);
    return true;
}

std::string Application::autopilotQuery(const std::string& what) {
    World& world = m_ctx->world;
    const EntityHandle primary = m_ctx->primary();
    const bool primaryAlive = world.isAlive(primary);
    if (what == "handles") return std::to_string(static_cast<int>(m_ctx->handles));
    if (what == "material") return primaryAlive ? std::to_string(world.getRenderable(primary).material_uuid) : std::string();
    if (what == "visible") return flag(world.isVisible(primary));
    if (what == "parent") return displayName(world, world.getParent(primary));
    if (what == "x" || what == "y" || what == "z") {
        const glm::vec3 position = world.getWorldPosition(primary);
        return formatFloat(what == "x" ? position.x : what == "y" ? position.y : position.z);
    }
    if (what == "palette") return flag(m_palette.isOpen());
    if (what == "onscreen") {
        const std::optional<glm::vec2> at = m_engine.worldToScreen(world.getWorldPosition(primary));
        const glm::vec2 size = m_engine.windowSize();
        return flag(at && at->x >= 0.0f && at->y >= 0.0f && at->x <= size.x && at->y <= size.y);
    }
    if (what == "zen") return flag(m_ctx->zen);
    if (what == "dirty") return flag(m_ctx->dirty());
    if (what == "inspector") return m_inspector.sections();
    if (what == "world") return flag(m_look.worldVisible());
    if (what == "ortho") return flag(world.camera().isOrthographic());
    if (what == "pie") return m_hud.pieOpen() ? m_hud.pieLabels() : "closed";
    if (what == "look") return flag(m_ctx->showLook);
    if (what == "prompt") return flag(m_unsavedPromptOpen);
    if (what == "rx" || what == "ry" || what == "rz") {
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::quat_cast(glm::mat3(world.getWorldMatrix(primary)))));
        return formatFloat(what == "rx" ? euler.x : what == "ry" ? euler.y : euler.z);
    }
    if (what == "toasts:error") return std::to_string(std::ranges::count_if(m_ctx->toasts, [](const Toast& toast) { return toast.kind == ToastKind::Error; }));
    if (what == "passes") {
        int enabled = 0;
        for (const PassInfo& pass : m_engine.customPasses()) enabled += pass.enabled ? 1 : 0;
        return std::to_string(enabled);
    }
    return autopilotQueryPrefixed(what);
}

std::string Application::autopilotQueryPrefixed(const std::string& what) {
    constexpr std::string_view kFilePrefix = "file:";
    constexpr std::string_view kRoughnessPrefix = "roughness:";
    constexpr std::string_view kShaderPrefix = "shader:";
    constexpr std::string_view kEffectPrefix = "effect:";
    if (what.starts_with(kFilePrefix)) return flag(std::filesystem::is_regular_file(what.substr(kFilePrefix.size())));
    if (what.starts_with(kRoughnessPrefix)) {
        const EntityHandle entity = m_ctx->world.find(what.substr(kRoughnessPrefix.size()));
        if (!m_ctx->world.isAlive(entity)) return "missing";
        return formatFloat(m_engine.material(m_ctx->world.getRenderable(entity).material_uuid).roughness);
    }
    if (what.starts_with(kShaderPrefix)) return m_engine.shaderStatus(what.substr(kShaderPrefix.size()));
    if (what.starts_with(kEffectPrefix)) {
        const std::string effectName = what.substr(kEffectPrefix.size());
        for (const PassInfo& pass : m_engine.customPasses()) {
            if (pass.kind != PassKind::Effect || pass.name != effectName) continue;
            if (!pass.valid) return "invalid";
            return pass.stage == EffectStage::Ldr ? "ldr" : "hdr";
        }
        return "missing";
    }
    return {};
}
