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

constexpr int kDebugViewCount = 11;
constexpr int kTonemapperCount = 6;
constexpr float kTimeStepHours = 0.5f;
constexpr float kHoursPerDay = 24.0f;
constexpr const char* kSurfaceShaderDirectory = "resources/shaders/surfaces";
constexpr const char* kSurfaceShaderTemplate = "void vertex(inout VertexData v, in VertexContext c) {\n}\n\nvoid surface(in SurfaceInput i, inout Surface s) {\n    s.baseColor *= 0.5 + 0.5 * litNoise(i.worldPosition * 4.0);\n}\n";

const char* const kDebugViewNames[kDebugViewCount] = {"Lit", "Base color", "Normals", "Roughness", "Metallic", "Ambient occlusion", "Sun shadows", "Reflections", "Lighting only", "Luminance heatmap", "Light clusters"};
const char* const kTonemapperNames[kTonemapperCount] = {"AgX", "AgX punchy", "ACES", "PBR Neutral", "Reinhard", "Linear"};

struct TimePreset {
    const char* name;
    float hour;
};

constexpr TimePreset kTimePresets[] = {{"Sunrise", 6.4f}, {"Morning", 9.0f}, {"Noon", 12.5f}, {"Golden hour", 17.4f}, {"Sunset", 18.3f}, {"Night", 22.5f}};

std::string sanitizeShaderName(const std::filesystem::path& given, const std::string& name) {
    std::string clean;
    for (char character : given.extension() == ".surface" ? given.stem().string() : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) || character == '_') clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return clean;
}

std::filesystem::path resolveShaderPath(const std::filesystem::path& given, const std::string& clean) {
    const bool bare = !given.has_parent_path();
    std::filesystem::path path = bare ? std::filesystem::path(kSurfaceShaderDirectory) / (clean + ".surface") : given;
#ifdef LIT_SOURCE_DIR
    const std::filesystem::path sourceSurfaces = std::filesystem::path(LIT_SOURCE_DIR) / kSurfaceShaderDirectory;
    if (bare && std::filesystem::is_directory(sourceSurfaces)) path = sourceSurfaces / (clean + ".surface");
    if (!bare && given.is_relative() && !std::filesystem::exists(path) && std::filesystem::exists(std::filesystem::path(LIT_SOURCE_DIR) / given)) path = std::filesystem::path(LIT_SOURCE_DIR) / given;
#endif
    return path;
}

bool writeShaderTemplate(const std::filesystem::path& path) {
    std::error_code directoryError;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), directoryError);
    std::ofstream out(path);
    out << kSurfaceShaderTemplate;
    return out.good();
}

}

void Application::registerSceneCommands() {
    addCommand("scene.showcase", "Load the PBR showcase scene", "File", {}, [this] {
        replaceSceneWith([this] {
            m_engine.portals().clear();
            buildShowcaseScene();
        }, "Showcase scene");
    });
    addCommand("scene.portalLoop", "Load the portal loop test scene", "File", {}, [this] { replaceSceneWith([this] { buildPortalScene(0); }, "Portal loop scene"); });
    addCommand("scene.portalTorture", "Load the nested portal torture scene", "File", {}, [this] { replaceSceneWith([this] { buildPortalScene(1); }, "Portal torture scene"); });
    addCommand("view.debugOverlay", "Debug overlay: portals, params, observation, AI, perf", "View", {key(ImGuiKey_F3)}, [this] { m_overlay.toggle(); }).checked = [this] { return m_overlay.visible(); };
    for (const std::string& testName : testSceneNames()) {
        addCommand("scene.test." + testName, "Load test scene: " + testName, "File", {}, [this, testName] {
            if (buildTestScene(testName)) m_ctx->toast("Test scene " + testName, ToastKind::Success);
        });
    }
}

void Application::registerRenderCommands() {
    addCommand("render.reload", "Reload shaders", "Render", {key(ImGuiKey_F9)}, [this] {
        m_engine.reloadShaders();
        m_ctx->toast("Shaders reloaded");
    }, {}, CommandScope::Global);
    addCommand("render.depth", "Hi-Z depth view", "Render", {}, [this] {
        m_lookState.debugDepth = !m_lookState.debugDepth;
        m_engine.setDebugDepthMode(m_lookState.debugDepth);
    }).checked = [this] { return m_lookState.debugDepth; };
    addCommand("render.lodUp", "Force a coarser LOD", "Render", {}, [this] {
        m_lookState.forcedLod = std::min(m_lookState.forcedLod + 1, static_cast<int>(kLodLevelCount) - 1);
        m_engine.setForcedLod(m_lookState.forcedLod);
    });
    addCommand("render.lodAuto", "Automatic LOD", "Render", {}, [this] {
        m_lookState.forcedLod = -1;
        m_engine.setForcedLod(-1);
    });
    addCommand("render.animate", "Animate stress objects", "Render", {}, [this] { m_lookState.animateStress = !m_lookState.animateStress; }).checked = [this] { return m_lookState.animateStress; };
}

void Application::registerDebugViewCommands() {
    for (int view = 0; view < kDebugViewCount; ++view) {
        addCommand(std::format("debug.view{}", view), std::format("Debug view: {}", kDebugViewNames[view]), "Render", {}, [this, view] { m_engine.renderSettings().debugView = static_cast<DebugView>(view); }).checked = [this, view] { return static_cast<int>(m_engine.renderSettings().debugView) == view; };
    }
    addCommand("debug.cycle", "Cycle debug view", "Render", {alt(ImGuiKey_L)}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.debugView = static_cast<DebugView>((static_cast<int>(settings.debugView) + 1) % kDebugViewCount);
        m_ctx->toast(std::format("View: {}", kDebugViewNames[static_cast<int>(settings.debugView)]));
    });
}

void Application::registerLightingCommands() {
    for (const TimePreset& preset : kTimePresets) {
        const float hour = preset.hour;
        addCommand(std::format("time.{}", preset.name), std::format("Time of day: {}", preset.name), "Lighting", {}, [this, hour] { m_engine.renderSettings().timeOfDay = hour; });
    }
    addCommand("time.animate", "Animate time of day", "Lighting", {ctrlShift(ImGuiKey_D)}, [this] { m_engine.renderSettings().animateTime = !m_engine.renderSettings().animateTime; }, {}, CommandScope::Global).checked = [this] { return m_engine.renderSettings().animateTime; };
    addCommand("time.later", "Sun later (+30 min)", "Lighting", {ctrl(ImGuiKey_RightArrow)}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.timeOfDay = std::fmod(settings.timeOfDay + kTimeStepHours, kHoursPerDay);
    }).repeat = true;
    addCommand("time.earlier", "Sun earlier (-30 min)", "Lighting", {ctrl(ImGuiKey_LeftArrow)}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.timeOfDay = std::fmod(settings.timeOfDay + (kHoursPerDay - kTimeStepHours), kHoursPerDay);
    }).repeat = true;
}

void Application::registerRenderToggleCommands() {
    addCommand("render.tonemapper", "Cycle tonemapper", "Render", {}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.tonemapper = static_cast<Tonemapper>((static_cast<int>(settings.tonemapper) + 1) % kTonemapperCount);
        m_ctx->toast(std::format("Tonemapper: {}", kTonemapperNames[static_cast<int>(settings.tonemapper)]));
    });
    auto toggle = [this](const char* id, const char* title, bool RenderSettings::*member) {
        addCommand(id, title, "Render", {}, [this, member] { m_engine.renderSettings().*member = !(m_engine.renderSettings().*member); }).checked = [this, member] { return m_engine.renderSettings().*member; };
    };
    toggle("render.shadows", "Shadows", &RenderSettings::shadows);
    toggle("render.ssao", "Ambient occlusion", &RenderSettings::ssao);
    toggle("render.colorBleed", "GI color bleeding", &RenderSettings::colorBleed);
    toggle("render.contactShadows", "Contact shadows", &RenderSettings::contactShadows);
    toggle("render.bentNormals", "Bent normals", &RenderSettings::bentNormals);
    toggle("render.areaLights", "Area lights", &RenderSettings::areaLights);
    toggle("render.governor", "Quality governor", &RenderSettings::qualityGovernor);
    addCommand("render.scale", "Cycle render scale", "Render", {}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.renderScale = settings.renderScale <= kRenderScaleMin + kRenderScaleStep * 0.5f ? kRenderScaleMax : quantizeRenderScale(settings.renderScale - kRenderScaleStep * 2.0f);
        m_ctx->toast(std::format("Render scale: {:.2f}", settings.renderScale));
    });
    addCommand("render.reflections", "Cycle reflection mode", "Render", {}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.reflectionMode = static_cast<ReflectionMode>((static_cast<int>(settings.reflectionMode) + 1) % kReflectionModeCount);
        m_ctx->toast(std::format("Reflections: {}", kReflectionModeNames[static_cast<int>(settings.reflectionMode)]));
    });
    addCommand("render.reflectionQuality", "Cycle reflection quality", "Render", {}, [this] {
        RenderSettings& settings = m_engine.renderSettings();
        settings.reflectionQuality = static_cast<ReflectionQuality>((static_cast<int>(settings.reflectionQuality) + 1) % kReflectionQualityCount);
        m_ctx->toast(std::format("Reflection quality: {}", kReflectionQualityNames[static_cast<int>(settings.reflectionQuality)]));
    });
    toggle("render.bloom", "Bloom", &RenderSettings::bloom);
    toggle("render.fxaa", "FXAA", &RenderSettings::fxaa);
    toggle("render.clouds", "Clouds", &RenderSettings::clouds);
    toggle("render.fog", "Fog and aerial perspective", &RenderSettings::fog);
    toggle("render.autoExposure", "Auto exposure", &RenderSettings::autoExposure);
}

void Application::registerPassCommands() {
    for (const PassInfo& pass : m_engine.customPasses()) {
        const PassKind kind = pass.kind;
        const uint32_t id = pass.id;
        const char* kindLabel = kind == PassKind::Effect ? "Effect" : kind == PassKind::Draw ? "Draw" : "Compute";
        const std::string title = std::format("{}: {}", kindLabel, pass.name);
        addCommand(std::format("pass.{}", id), "Toggle " + title, "Shaders", {}, [this, kind, id] {
            for (const PassInfo& candidate : m_engine.customPasses()) {
                if (candidate.id == id) m_engine.setPassEnabled(kind, id, !candidate.enabled);
            }
        }).checked = [this, id] {
            for (const PassInfo& candidate : m_engine.customPasses()) {
                if (candidate.id == id) return candidate.enabled;
            }
            return false;
        };
    }
}

void Application::createShaderFromPrompt(const std::string& name) {
    const std::filesystem::path given(name);
    const std::string clean = sanitizeShaderName(given, name);
    if (clean.empty()) {
        m_ctx->toast(std::format("'{}' is not a valid shader name", name), ToastKind::Error);
        return;
    }
    const std::filesystem::path path = resolveShaderPath(given, clean);
    if (!std::filesystem::exists(path)) {
        if (!writeShaderTemplate(path)) {
            m_ctx->toast(std::format("Cannot create {}", path.string()), ToastKind::Error);
            return;
        }
        m_ctx->toast(std::format("Created {} from the template", path.string()));
    } else if (!std::filesystem::is_regular_file(path)) {
        m_ctx->toast(std::format("{} is not a file", path.string()), ToastKind::Error);
        return;
    }
    const uint32_t shaderId = m_engine.loadShader(clean, path.string());
    m_ctx->history->beginGroup("Assign shader");
    for (EntityHandle entity : m_ctx->topLevelSelection()) {
        const RenderableComponent renderable = m_ctx->world.getRenderable(entity);
        m_ctx->history->setRender(entity, m_ctx->world.getMesh(entity), renderable.material_uuid, shaderId, renderable.alpha);
    }
    m_ctx->history->endGroup();
    if (m_engine.shaderStatus(clean) == "failed") return;
    m_ctx->toast(std::format("Edit {} and save: it hot-reloads", path.string()), ToastKind::Success);
}

void Application::registerShaderCommands() {
    addCommand("shader.new", "New surface shader for selection...", "Shaders", {}, [this] {
        m_palette.prompt({"New surface shader name (or path to a .surface file)", "", "e.g. rusty", {}, [this](const std::string& name) { createShaderFromPrompt(name); }});
    });
}

void Application::registerProfileCommands() {
    addCommand("profile.cpu", "CPU profiler", "Profile", {key(ImGuiKey_F10)}, [this] { m_engine.profiler().setEnabled(!m_engine.profiler().enabled()); }, {}, CommandScope::Global).checked = [this] { return m_engine.profiler().enabled(); };
    addCommand("profile.trace", "Write Chrome trace", "Profile", {key(ImGuiKey_F11)}, [this] {
        if (m_engine.profiler().writeChromeTrace("resources/trace.json")) m_ctx->toast("Trace written to resources/trace.json", ToastKind::Success);
    }, {}, CommandScope::Global);
}
