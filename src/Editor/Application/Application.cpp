module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
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
#include <system_error>
#include <vector>

#include "Engine/Log/Log.hpp"

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

constexpr const char* kAutosavePath = "resources/autosave.litscene";

size_t envSize(const char* name, size_t fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    return static_cast<size_t>(std::strtoull(value, nullptr, 10));
}

}  // namespace

Application::Application() {
    if (!m_engine.open({"Lit Editor", 1600, 900})) return;
    m_window = m_engine.window();
    m_engine.enableImGui();
    Lit::Log::Phase("imgui");
    float scaleX = 1.0f, scaleY = 1.0f;
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
    const char* sceneChoice = std::getenv("LIT_SCENE");
    const bool showcase = sceneChoice && std::string(sceneChoice) == "showcase";
    Camera& camera = m_ctx->world.camera();
    camera.setFarPlane(2000.0f);
    const bool portalLoop = sceneChoice && std::string(sceneChoice) == "portal_loop";
    const bool portalTorture = sceneChoice && std::string(sceneChoice) == "portal_torture";
    if (portalLoop || portalTorture) {
        buildPortalScene(portalTorture ? 1 : 0);
    } else if (showcase) {
        buildShowcaseScene();
    } else {
        buildStressScene();
        camera.setPos(glm::vec3(200.0f, 1000.0f, 200.0f));
        camera.setOrientation(-135.0f, -74.2f);
    }
    Lit::Log::Phase(showcase ? "showcase scene" : "stress scene");
    m_engine.setFullProfiling(true);
    m_engine.setProfilingHud(false);
    m_engine.setPaused(true);
    m_engine.setShaderWatch(true);
    if (const char* interval = std::getenv("LIT_AUTOSAVE_SECONDS")) m_autosaveInterval = static_cast<float>(std::atof(interval));
    m_recoveryPending = std::filesystem::exists(kAutosavePath);
    if (std::getenv("LIT_PROFILE")) m_engine.profiler().setEnabled(true);

    if (const char* camPos = std::getenv("LIT_CAM_POS")) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (std::sscanf(camPos, "%f,%f,%f", &x, &y, &z) == 3) camera.setPos(glm::vec3(x, y, z));
    }
    if (const char* camPitch = std::getenv("LIT_CAM_PITCH")) {
        const char* camYaw = std::getenv("LIT_CAM_YAW");
        camera.setOrientation(camYaw ? static_cast<float>(std::atof(camYaw)) : -90.0f, static_cast<float>(std::atof(camPitch)));
    }
    if (const char* forceLod = std::getenv("LIT_FORCE_LOD")) m_lookState.forcedLod = std::atoi(forceLod);
    if (const char* lodBias = std::getenv("LIT_LOD_BIAS")) m_lookState.lodBias = static_cast<float>(std::atof(lodBias));
    m_engine.setForcedLod(m_lookState.forcedLod);
    m_engine.setLodBias(m_lookState.lodBias);

    m_engine.loadShaderTexture("u_lut", "resources/textures/grade_warm.png", false);
    for (const char* effect : {"outline", "sharpen", "chromatic", "underwater", "grade"}) {
        const uint32_t id = m_engine.addEffect(effect);
        m_engine.setPassEnabled(PassKind::Effect, id, false);
    }
    registerCommands();
    m_hud.setPieItems({{"Move", "transform.move"}, {"Rotate", "transform.rotate"}, {"Scale", "transform.scale"}, {"Duplicate", "edit.duplicate"}, {"Delete", "edit.delete"}, {"Focus", "view.focus"}, {"Add...", "create.menu"}, {"Hide", "hide.selected"}},
                       {{"Add...", "create.menu"}, {"Select all", "select.all"}, {"Paste", "edit.paste"}, {"Frame all", "view.frameAll"}, {"World", "view.world"}, {"Ortho", "view.ortho"}, {"Reveal all", "hide.revealAll"}, {"Look", "view.look"}});
    m_ctx->savedRevision = m_ctx->history->state();
    m_stableSettings = m_engine.renderSettings();
    m_lastFrame = glfwGetTime();
    if (const char* script = std::getenv("LIT_AUTOPILOT")) {
        m_recoveryPending = false;
        m_autopilot.load(script);
        m_autopilot.window = m_window;
        m_autopilot.saveScene = [this](const std::string& path) { saveScene(path); };
        m_autopilot.loadScene = [this](const std::string& path) { loadScene(path); };
        m_autopilot.editMaterial = [this](const std::string& field, float value) {
            const uint32_t id = m_ctx->world.getRenderable(m_ctx->primary()).material_uuid;
            Material m = m_engine.material(id);
            if (field == "roughness") m.roughness = value;
            else if (field == "metallic") m.metallic = value;
            else if (field == "opacity") m.opacity = value;
            else if (field == "clearcoat") m.clearcoat = value;
            else return false;
            m_engine.setMaterial(id, m);
            return true;
        };
        m_autopilot.query = [this](const std::string& what) -> std::string {
            if (what == "handles") return std::to_string(static_cast<int>(m_ctx->handles));
            if (what == "material") return std::to_string(m_ctx->world.getRenderable(m_ctx->primary()).material_uuid);
            if (what == "visible") return m_ctx->world.isVisible(m_ctx->primary()) ? "1" : "0";
            if (what == "parent") return displayName(m_ctx->world, m_ctx->world.getParent(m_ctx->primary()));
            if (what == "x") return std::format("{:.2f}", m_ctx->world.getWorldPosition(m_ctx->primary()).x);
            if (what == "y") return std::format("{:.2f}", m_ctx->world.getWorldPosition(m_ctx->primary()).y);
            if (what == "z") return std::format("{:.2f}", m_ctx->world.getWorldPosition(m_ctx->primary()).z);
            if (what == "palette") return m_palette.isOpen() ? "1" : "0";
            if (what == "onscreen") {
                const std::optional<glm::vec2> at = m_engine.worldToScreen(m_ctx->world.getWorldPosition(m_ctx->primary()));
                const glm::vec2 size = m_engine.windowSize();
                return at && at->x >= 0.0f && at->y >= 0.0f && at->x <= size.x && at->y <= size.y ? "1" : "0";
            }
            if (what.rfind("file:", 0) == 0) return std::filesystem::is_regular_file(what.substr(5)) ? "1" : "0";
            if (what == "zen") return m_ctx->zen ? "1" : "0";
            if (what == "dirty") return m_ctx->dirty() ? "1" : "0";
            if (what == "inspector") return m_inspector.sections();
            if (what == "world") return m_look.worldVisible() ? "1" : "0";
            if (what == "ortho") return m_ctx->world.camera().isOrthographic() ? "1" : "0";
            if (what == "pie") return m_hud.pieOpen() ? m_hud.pieLabels() : "closed";
            if (what == "look") return m_ctx->showLook ? "1" : "0";
            if (what == "prompt") return m_unsavedPromptOpen ? "1" : "0";
            if (what == "rx" || what == "ry" || what == "rz") {
                const glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::quat_cast(glm::mat3(m_ctx->world.getWorldMatrix(m_ctx->primary())))));
                return std::format("{:.2f}", what == "rx" ? euler.x : what == "ry" ? euler.y : euler.z);
            }
            if (what.rfind("roughness:", 0) == 0) {
                const EntityHandle e = m_ctx->world.find(what.substr(10));
                return m_ctx->world.isAlive(e) ? std::format("{:.2f}", m_engine.material(m_ctx->world.getRenderable(e).material_uuid).roughness) : "missing";
            }
            if (what.rfind("shader:", 0) == 0) return m_engine.shaderStatus(what.substr(7));
            if (what == "toasts:error") return std::to_string(std::ranges::count_if(m_ctx->toasts, [](const Toast& t) { return t.kind == ToastKind::Error; }));
            if (what.rfind("effect:", 0) == 0) {
                for (const PassInfo& pass : m_engine.customPasses()) {
                    if (pass.kind == PassKind::Effect && pass.name == what.substr(7)) return !pass.valid ? "invalid" : pass.stage == EffectStage::Ldr ? "ldr" : "hdr";
                }
                return "missing";
            }
            if (what == "passes") {
                int enabled = 0;
                for (const PassInfo& pass : m_engine.customPasses()) enabled += pass.enabled ? 1 : 0;
                return std::to_string(enabled);
            }
            return {};
        };
    }
    Lit::Log::Phase("editor ready");
}

Application::~Application() {
    std::error_code removeError;
    if (m_ctx && (!m_ctx->dirty() || m_discardConfirmed)) std::filesystem::remove(kAutosavePath, removeError);
    m_ctx.reset();
    m_engine.cleanup();
}

bool Application::isRunning() const { return m_window && m_ctx && !m_ctx->requestQuit; }

void Application::guarded(std::function<void()> action) {
    if (!m_ctx->dirty()) {
        action();
        return;
    }
    m_pendingAction = std::move(action);
    m_unsavedPromptOpen = true;
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
    if (ImGui::Button("Save  (Enter)", ImVec2(140.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        saveScene(m_ctx->scenePath);
        finish(!m_ctx->dirty());
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't save  (D)", ImVec2(140.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        m_discardConfirmed = true;
        finish(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel  (Esc)", ImVec2(140.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) finish(false);
    ImGui::EndPopup();
}

void Application::trackRenderSettings() {
    RenderSettings current = m_engine.renderSettings();
    if (ImGui::IsAnyItemActive()) return;
    RenderSettings stable = m_stableSettings;
    if (current.animateTime) stable.timeOfDay = current.timeOfDay;
    if (current == stable) return;
    const RenderSettings before = m_stableSettings;
    m_stableSettings = current;
    m_ctx->history->record("Render settings", [this, current] { m_engine.renderSettings() = m_stableSettings = current; }, [this, before] { m_engine.renderSettings() = m_stableSettings = before; });
}

void Application::buildStressScene() {
    World& world = m_ctx->world;
    const size_t numObjects = envSize("LIT_OBJECTS", 8000000);
    const size_t moving = std::min(envSize("LIT_MOVING", 1500000), numObjects);
    Lit::Log::Info("Creating {} random objects...", numObjects);
    std::random_device rd;
    std::mt19937 gen(std::getenv("LIT_SEED") ? static_cast<unsigned>(std::atoi(std::getenv("LIT_SEED"))) : rd());
    std::uniform_real_distribution<float> distribPosHeight(-550.0f, 550.0f);
    std::uniform_real_distribution<float> distribPosSideX(-230.0f, 230.0f);
    std::uniform_real_distribution<float> distribPosSideZ(-230.0f, 230.0f);
    std::uniform_int_distribution<unsigned int> distribType(0, 1);

    const auto createStart = std::chrono::steady_clock::now();
    world.reserve(numObjects + 1);
    EntityDesc parentDesc;
    parentDesc.name = "parent";
    parentDesc.mesh = m_ctx->cubeMesh;
    m_ctx->stressParent = world.create(parentDesc);
    m_movingObjectCount = static_cast<uint32_t>(moving);
    m_basePositions.resize(m_movingObjectCount);
    const uint32_t cube = m_ctx->cubeMesh;
    const uint32_t sphere = m_ctx->sphereMesh;
    const EntityHandle parent = m_ctx->stressParent;
    std::vector<uint32_t> palette;
    std::mt19937 paletteRng(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < 16; ++i) {
        Material m;
        m.name = std::format("Stress {}", i);
        const float hue = static_cast<float>(i) / 16.0f;
        const glm::vec3 k = glm::vec3(std::fmod(hue + 1.0f, 1.0f), std::fmod(hue + 2.0f / 3.0f, 1.0f), std::fmod(hue + 1.0f / 3.0f, 1.0f)) * 6.0f;
        const glm::vec3 rgb = glm::clamp(glm::abs(glm::vec3(std::fmod(k.x, 6.0f), std::fmod(k.y, 6.0f), std::fmod(k.z, 6.0f)) - glm::vec3(3.0f)) - glm::vec3(1.0f), glm::vec3(0.0f), glm::vec3(1.0f));
        m.baseColor = glm::vec3(0.15f) + rgb * 0.75f;
        m.metallic = i % 4 == 0 ? 1.0f : 0.0f;
        m.roughness = 0.15f + unit(paletteRng) * 0.7f;
        m.clearcoat = i % 5 == 0 ? 1.0f : 0.0f;
        palette.push_back(m_engine.createMaterial(m));
    }
    world.createBatch(numObjects, [&](size_t i, EntityDesc& desc) {
        desc.position = glm::vec3(distribPosSideX(gen), distribPosHeight(gen), distribPosSideZ(gen));
        if (i < m_movingObjectCount) m_basePositions[i] = desc.position;
        const bool isTransparent = (i % 5 == 0);
        desc.mesh = (distribType(gen) == 1) ? sphere : cube;
        desc.shader = isTransparent ? 2 : (i % 7 == 0 ? 1 : 0);
        desc.material = palette[i % palette.size()];
        desc.alpha = isTransparent ? 0.6f : 1.0f;
        if (i < numObjects / 2) desc.parent = parent;
    });
    Lit::Log::Info("Created {} objects in {:.1f} ms", numObjects, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - createStart).count());
    m_engine.uploadBasePositions(m_basePositions);
}

void Application::buildShowcaseScene() {
    EditorContext& ctx = *m_ctx;
    World& world = ctx.world;
    Engine& engine = m_engine;
    const uint32_t sphere = engine.loadMesh("sphere_hd");
    const uint32_t torus = engine.loadMesh("torus");
    const uint32_t plane = engine.loadMesh("plane");
    const uint32_t cube = ctx.cubeMesh;
    auto material = [&](const std::string& name, auto&& edit) {
        Material m;
        m.name = name;
        edit(m);
        return engine.createMaterial(m);
    };
    auto shader = [&](const std::string& name) {
        const std::vector<std::string> names = engine.shaderNames();
        for (uint32_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) return i;
        }
        return 0u;
    };
    auto spawn = [&](const std::string& name, uint32_t mesh, uint32_t mat, const glm::vec3& position, const glm::vec3& scale, uint32_t surface = 0, float alpha = 1.0f) {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = mesh;
        desc.material = mat;
        desc.shader = surface;
        desc.alpha = alpha;
        desc.position = position;
        desc.scale = scale;
        return world.create(desc);
    };
    const uint32_t ground = material("Ground", [](Material& m) {
        m.baseColor = glm::vec3(0.42f, 0.42f, 0.40f);
        m.roughness = 0.75f;
        m.uvScale = 40.0f;
    });
    spawn("Ground", plane, ground, glm::vec3(0.0f), glm::vec3(60.0f, 1.0f, 60.0f), shader("Checker"));
    for (int i = 0; i < 7; ++i) {
        const float r = static_cast<float>(i) / 6.0f;
        const float x = -9.0f + static_cast<float>(i) * 3.0f;
        const uint32_t gold = material(std::format("Gold r{:.2f}", r), [r](Material& m) {
            m.baseColor = glm::vec3(1.0f, 0.766f, 0.336f);
            m.metallic = 1.0f;
            m.roughness = r;
        });
        const uint32_t plastic = material(std::format("Plastic r{:.2f}", r), [r](Material& m) {
            m.baseColor = glm::vec3(0.72f, 0.08f, 0.06f);
            m.metallic = 0.0f;
            m.roughness = r;
        });
        spawn(std::format("Gold {}", i), sphere, gold, glm::vec3(x, 1.0f, -3.0f), glm::vec3(1.0f));
        spawn(std::format("Plastic {}", i), sphere, plastic, glm::vec3(x, 1.0f, 0.5f), glm::vec3(1.0f));
    }
    const uint32_t carPaint = material("Car paint", [](Material& m) {
        m.baseColor = glm::vec3(0.05f, 0.18f, 0.55f);
        m.metallic = 0.6f;
        m.roughness = 0.45f;
        m.clearcoat = 1.0f;
        m.clearcoatRoughness = 0.03f;
    });
    const uint32_t velvet = material("Velvet", [](Material& m) {
        m.baseColor = glm::vec3(0.35f, 0.03f, 0.12f);
        m.roughness = 0.85f;
        m.sheenColor = glm::vec3(1.0f, 0.55f, 0.75f);
        m.sheenRoughness = 0.3f;
    });
    const uint32_t brushed = material("Brushed steel", [](Material& m) {
        m.baseColor = glm::vec3(0.92f, 0.92f, 0.93f);
        m.metallic = 1.0f;
        m.roughness = 0.35f;
        m.anisotropy = 0.85f;
    });
    const uint32_t skin = material("Wax", [](Material& m) {
        m.baseColor = glm::vec3(0.9f, 0.75f, 0.55f);
        m.roughness = 0.5f;
        m.subsurface = 0.8f;
        m.subsurfaceColor = glm::vec3(1.0f, 0.35f, 0.15f);
        m.thickness = 0.3f;
    });
    const uint32_t glass = material("Glass", [](Material& m) {
        m.baseColor = glm::vec3(0.8f, 0.95f, 1.0f);
        m.roughness = 0.03f;
        m.opacity = 0.3f;
        m.transmission = 1.0f;
    });
    const uint32_t neutral = material("Clay", [](Material& m) {
        m.baseColor = glm::vec3(0.75f);
        m.roughness = 0.6f;
    });
    const uint32_t emissive = material("Neon", [](Material& m) {
        m.baseColor = glm::vec3(0.1f);
        m.emissive = glm::vec3(0.2f, 0.8f, 1.0f);
        m.emissiveIntensity = 40000.0f;
    });
    spawn("Car paint torus", torus, carPaint, glm::vec3(-9.0f, 0.4f, 5.0f), glm::vec3(1.2f));
    spawn("Velvet", sphere, velvet, glm::vec3(-6.0f, 1.0f, 5.0f), glm::vec3(1.0f));
    const EntityHandle brushedTorus = spawn("Brushed torus", torus, brushed, glm::vec3(-3.0f, 1.3f, 5.0f), glm::vec3(1.2f));
    world.setRotation(brushedTorus, glm::angleAxis(glm::radians(70.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    spawn("Wax", sphere, skin, glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(1.0f));
    spawn("Lava", sphere, neutral, glm::vec3(3.0f, 1.2f, 5.0f), glm::vec3(1.0f), shader("Lava"));
    spawn("Marble", sphere, neutral, glm::vec3(6.0f, 1.0f, 5.0f), glm::vec3(1.0f), shader("Marble"));
    spawn("Glass", sphere, glass, glm::vec3(9.0f, 1.0f, 5.0f), glm::vec3(1.0f), shader("Glass"));
    spawn("Tinted", sphere, neutral, glm::vec3(10.5f, 1.0f, 8.0f), glm::vec3(1.0f), 0, 0.5f);
    spawn("Neon bar", cube, emissive, glm::vec3(0.0f, 0.25f, 9.0f), glm::vec3(8.0f, 0.12f, 0.12f));
    spawn("Monolith", cube, neutral, glm::vec3(13.0f, 3.0f, 0.0f), glm::vec3(0.6f, 3.0f, 2.0f));
    spawn("Wall", cube, neutral, glm::vec3(0.0f, 2.0f, -8.0f), glm::vec3(14.0f, 2.0f, 0.3f));
    auto light = [&](const std::string& name, const glm::vec3& position, const std::string& text) {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = NO_MESH;
        desc.position = position;
        const EntityHandle e = world.create(desc);
        world.setComponentFromText(e, "Light", text);
        return e;
    };
    light("Warm lamp", glm::vec3(-7.0f, 2.5f, 2.5f), "1 1 0.62 0.3 8000 18 1 20 30");
    light("Cyan lamp", glm::vec3(7.0f, 2.0f, 2.5f), "1 0.3 0.8 1 6000 16 1 20 30");
    struct Particle {
        glm::vec4 position;
        glm::vec4 velocity;
    };
    constexpr uint32_t kParticles = 20000;
    std::vector<Particle> particles(kParticles);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
    for (Particle& p : particles) {
        const glm::vec3 offset(spread(rng) * 4.0f, spread(rng) * 1.5f, spread(rng) * 4.0f);
        p.position = glm::vec4(glm::vec3(0.0f, 3.5f, 2.0f) + offset, std::abs(spread(rng)));
        p.velocity = glm::vec4(glm::cross(glm::normalize(offset + glm::vec3(1e-3f)), glm::vec3(0.0f, 1.0f, 0.0f)) * 2.5f, 0.0f);
    }
    engine.setShaderBuffer("Particles", particles);
    const uint32_t simulate = engine.addCompute({"Particle simulation", "resources/shaders/compute/particles.comp", {(kParticles + 255) / 256, 1, 1}, ComputeStage::BeforeScene});
    engine.setPassParam(PassKind::Compute, simulate, 0, glm::vec4(0.0f, 3.5f, 2.0f, static_cast<float>(kParticles)));
    DrawDesc sparks;
    sparks.name = "Particles";
    sparks.vertexPath = "resources/shaders/draws/particles.vert";
    sparks.fragmentPath = "resources/shaders/draws/particles.frag";
    sparks.primitive = DrawPrimitive::Points;
    sparks.vertexCount = kParticles;
    sparks.blend = DrawBlend::Additive;
    sparks.depthWrite = false;
    engine.addDraw(sparks);
    if (engine.meshShadersSupported()) {
        DrawDesc wave;
        wave.name = "Mesh shader wave";
        wave.meshPath = "resources/shaders/draws/wave.mesh";
        wave.fragmentPath = "resources/shaders/draws/wave.frag";
        wave.meshGroups = glm::ivec3(32, 1, 1);
        const uint32_t waveId = engine.addDraw(wave);
        engine.setPassParam(PassKind::Draw, waveId, 0, glm::vec4(-22.0f, 0.2f, -2.0f, 0.0f));
    }
    m_movingObjectCount = 0;
    RenderSettings& settings = engine.renderSettings();
    settings.timeOfDay = 16.8f;
    settings.sunAzimuth = 200.0f;
    Camera& camera = world.camera();
    camera.setPos(glm::vec3(0.0f, 4.5f, 17.0f));
    camera.setOrientation(-90.0f, -14.0f);
    camera.setFarPlane(1500.0f);
}

std::vector<std::string> Application::sceneFiles() const {
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator("resources", ec)) {
        if (entry.path().extension() == ".litscene") files.push_back(entry.path().generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

void Application::buildPortalScene(int which) {
    EditorContext& ctx = *m_ctx;
    Engine& engine = m_engine;
    PortalSceneKit kit;
    kit.cube = ctx.cubeMesh;
    kit.sphere = ctx.sphereMesh;
    const std::vector<std::string> names = engine.shaderNames();
    for (uint32_t i = 0; i < names.size(); ++i) {
        if (names[i] == "Checker") kit.floorShader = i;
    }
    kit.material = [&engine](const glm::vec3& color, const glm::vec3& emissive, float intensity, float roughness) {
        Material m;
        m.name = "Portal scene";
        m.baseColor = color;
        m.roughness = roughness;
        m.emissive = emissive;
        m.emissiveIntensity = intensity;
        return engine.createMaterial(m);
    };
    const PortalTestScene scene = which == 1 ? PortalTestScene::Torture : PortalTestScene::Loop;
    ::buildPortalScene(engine.portals(), ctx.world, kit, scene);
    float yaw = 0.0f;
    float pitch = 0.0f;
    const glm::vec3 start = portalSceneStart(scene, yaw, pitch);
    Camera& camera = ctx.world.camera();
    camera.setPos(start);
    camera.setOrientation(yaw, pitch);
    camera.setNearPlane(0.05f);
    engine.portals().setCameraCell(engine.portals().cellAt(start));
}

void Application::saveScene(const std::string& path) {
    const auto start = std::chrono::steady_clock::now();
    if (m_ctx->world.saveScene(path)) {
        m_ctx->scenePath = path;
        m_ctx->savedRevision = m_ctx->history->state();
        m_ctx->toast(std::format("Saved {} ({:.0f} ms)", path, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()), ToastKind::Success);
    } else {
        m_ctx->toast("Could not save " + path, ToastKind::Error);
    }
}

void Application::afterSceneReplaced() {
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
    if (m_ctx->world.loadScene(path)) {
        m_engine.portals().clear();
        m_ctx->scenePath = path;
        afterSceneReplaced();
        m_ctx->toast(std::format("Opened {} ({:.0f} ms)", path, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()), ToastKind::Success);
    } else {
        m_ctx->toast("Could not open " + path, ToastKind::Error);
    }
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
        ctx.toast(std::format("Playing ({:.0f} ms). F5 stops and restores the scene", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()), ToastKind::Success);
    } else {
        m_engine.setPaused(true);
        ctx.world.restoreSnapshot(ctx.playSnapshot);
        const std::vector<EntityHandle> kept = m_playSelection;
        ctx.playSnapshot = WorldSnapshot{};
        m_stableSettings = m_engine.renderSettings();
        if (m_editHistory) ctx.history = std::move(m_editHistory);
        ctx.selection.clear();
        for (EntityHandle e : kept) {
            if (ctx.world.isAlive(e)) ctx.selection.add(e);
        }
        ctx.stressParent = ctx.world.find("parent");
        ctx.paused = false;
        ctx.toast(std::format("Stopped, scene restored ({:.0f} ms)", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()));
    }
    ctx.playing = playing;
}

void Application::duplicateSelection(bool grab) {
    EditorContext& ctx = *m_ctx;
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    std::vector<EntityHandle> copies;
    ctx.history->beginGroup(top.size() > 1 ? std::format("Duplicate x{}", top.size()) : "Duplicate");
    for (EntityHandle e : top) {
        const Prefab prefab = ctx.world.capture(e);
        copies.push_back(ctx.history->instantiate(prefab, ctx.world.getParent(e)));
    }
    ctx.history->endGroup();
    ctx.selection.setAll(copies);
    if (grab) m_tool.begin(ctx, ModalKind::Move);
}

void Application::deleteSelection() {
    EditorContext& ctx = *m_ctx;
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    ctx.history->beginGroup(top.size() > 1 ? std::format("Delete x{}", top.size()) : "Delete");
    for (EntityHandle e : top) ctx.history->destroy(e);
    ctx.history->endGroup();
    ctx.selection.clear();
    ctx.toast(std::format("Deleted {} (Ctrl+Z to restore)", top.size()));
}

void Application::copySelection() {
    EditorContext& ctx = *m_ctx;
    ctx.clipboard.clear();
    for (EntityHandle e : ctx.topLevelSelection()) ctx.clipboard.push_back(ctx.world.capture(e));
    if (!ctx.clipboard.empty()) ctx.toast(std::format("Copied {}", ctx.clipboard.size()));
}

void Application::paste() {
    EditorContext& ctx = *m_ctx;
    if (ctx.clipboard.empty()) return;
    glm::vec3 center(0.0f);
    for (const Prefab& p : ctx.clipboard) center += glm::vec3(p.nodes.front().local[3]);
    center /= static_cast<float>(ctx.clipboard.size());
    const glm::vec3 offset = spawnPoint(ctx).position - center;
    std::vector<EntityHandle> created;
    ctx.history->beginGroup("Paste");
    for (Prefab p : ctx.clipboard) {
        p.nodes.front().local[3] += glm::vec4(offset, 0.0f);
        created.push_back(ctx.history->instantiate(p));
    }
    ctx.history->endGroup();
    ctx.selection.setAll(created);
}

EntityHandle Application::createPrimitive(const std::string& kind) {
    EditorContext& ctx = *m_ctx;
    EntityDesc desc;
    const SpawnPoint spawn = spawnPoint(ctx);
    desc.position = spawn.position + glm::vec3(0.0f, spawn.onSurface ? 0.5f : 0.0f, 0.0f);
    desc.name = kind;
    if (kind == "Cube") {
        desc.mesh = ctx.cubeMesh;
    } else if (kind == "Sphere") {
        desc.mesh = ctx.sphereMesh;
    } else if (kind == "Empty" || kind.find("light") != std::string::npos || kind == "Camera") {
        desc.mesh = NO_MESH;
    } else {
        desc.mesh = m_engine.loadMesh(kind);
    }
    const EntityHandle e = ctx.history->create(desc);
    auto addComponent = [&](const char* name, const std::string& text) {
        ctx.world.setComponentFromText(e, name, text);
    };
    if (kind == "Point light") addComponent("Light", "1 1 0.95 0.9 40 25 1 20 30");
    if (kind == "Spot light") {
        addComponent("Light", "2 1 1 1 80 40 1 18 28");
        ctx.world.setRotation(e, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    if (kind == "Sun light") {
        addComponent("Light", "0 1 0.96 0.9 3 100 1 20 30");
        ctx.world.setRotation(e, glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    }
    if (kind == "Camera") addComponent("Camera", "60 0.1 2000");
    ctx.selection.set(e);
    m_outliner.reveal(ctx, e);
    return e;
}

void Application::groupSelection() {
    EditorContext& ctx = *m_ctx;
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    glm::vec3 center(0.0f);
    for (EntityHandle e : top) center += ctx.world.getWorldPosition(e);
    center /= static_cast<float>(top.size());
    ctx.history->beginGroup("Group");
    EntityDesc desc;
    desc.name = "Group";
    desc.position = center;
    desc.parent = ctx.world.getParent(top.front());
    const EntityHandle group = ctx.history->create(desc);
    for (EntityHandle e : top) ctx.history->setParent(e, group);
    ctx.history->endGroup();
    ctx.selection.set(group);
    m_outliner.reveal(ctx, group);
}

void Application::parentToActive() {
    EditorContext& ctx = *m_ctx;
    const EntityHandle active = ctx.primary();
    if (ctx.selection.size() < 2) {
        ctx.toast("Select children first, then the new parent last", ToastKind::Warning);
        return;
    }
    ctx.history->beginGroup("Parent");
    for (EntityHandle e : ctx.selection.items()) {
        if (e != active && !ctx.world.isDescendantOf(active, e)) ctx.history->setParent(e, active);
    }
    ctx.history->endGroup();
    m_outliner.reveal(ctx, active);
}

void Application::walk(int direction) {
    EditorContext& ctx = *m_ctx;
    World& world = ctx.world;
    const EntityHandle current = ctx.primary();
    if (!world.isAlive(current)) {
        ctx.selection.set(world.firstRoot());
        return;
    }
    EntityHandle next;
    if (direction == 2) {
        next = world.getParent(current);
    } else if (direction == -2) {
        next = world.firstChild(current);
    } else if (direction == 1) {
        next = world.nextSibling(current);
        if (next.isNull()) {
            const EntityHandle parent = world.getParent(current);
            next = parent.isNull() ? world.firstRoot() : world.firstChild(parent);
        }
    } else {
        const EntityHandle parent = world.getParent(current);
        EntityHandle probe = parent.isNull() ? world.firstRoot() : world.firstChild(parent);
        EntityHandle previous;
        size_t guard = 0;
        while (!probe.isNull() && probe != current && guard++ < 2000000) {
            previous = probe;
            probe = world.nextSibling(probe);
        }
        next = previous;
    }
    if (world.isAlive(next)) {
        ctx.selection.set(next);
        m_outliner.reveal(ctx, next);
    }
}

void Application::frameSelection() {
    EditorContext& ctx = *m_ctx;
    if (ctx.selection.empty()) {
        frameAll();
        return;
    }
    std::vector<glm::vec4> spheres;
    for (EntityHandle e : ctx.selection.items()) {
        if (spheres.size() > 4096) break;
        const glm::vec4 b = ctx.world.getWorldBounds(e);
        spheres.push_back(b.w > 0.0f ? b : glm::vec4(ctx.world.getWorldPosition(e), 0.5f));
    }
    flyTo(ctx, gizmo::enclosingSphere(spheres));
}

void Application::frameAll() {
    if (const std::optional<glm::vec4> bounds = m_ctx->world.sceneBounds()) flyTo(*m_ctx, *bounds);
}

void Application::setView(const glm::vec3& direction) {
    EditorContext& ctx = *m_ctx;
    Camera& camera = ctx.world.camera();
    const glm::vec3 pivot = ctx.selection.empty() ? (ctx.cursorOnSurface ? ctx.cursor : camera.getPosition() + cameraForward(camera) * 20.0f) : ctx.world.getWorldPosition(ctx.primary());
    const float distance = std::max(glm::length(camera.getPosition() - pivot), 5.0f);
    const glm::vec3 d = glm::normalize(direction + glm::vec3(0.0f, 0.0f, 1e-4f));
    ctx.flight.active = true;
    ctx.flight.t = 0.0f;
    ctx.flight.from = camera.getPosition();
    ctx.flight.to = pivot - d * distance;
    ctx.flight.fromYaw = camera.getYaw();
    ctx.flight.fromPitch = camera.getPitch();
    ctx.flight.toYaw = glm::degrees(std::atan2(d.z, d.x));
    ctx.flight.toPitch = std::clamp(glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f))), -89.0f, 89.0f);
    ctx.flight.fromOrtho = camera.isOrthographic() ? camera.getOrthoHeight() : 0.0f;
    ctx.flight.toOrtho = 2.0f * distance * std::tan(glm::radians(camera.getFov()) * 0.5f);
    ctx.autoOrtho = true;
}

void Application::toggleOrthographic() {
    EditorContext& ctx = *m_ctx;
    Camera& camera = ctx.world.camera();
    ctx.flight.active = false;
    ctx.autoOrtho = false;
    if (camera.isOrthographic()) {
        camera.setOrthographic(false);
        return;
    }
    const glm::vec3 pivot = ctx.selection.empty() ? (ctx.cursorOnSurface ? ctx.cursor : camera.getPosition() + cameraForward(camera) * 10.0f) : ctx.world.getWorldPosition(ctx.primary());
    const float distance = std::max(glm::dot(pivot - camera.getPosition(), cameraForward(camera)), 1.0f);
    camera.setOrthoHeight(2.0f * distance * std::tan(glm::radians(camera.getFov()) * 0.5f));
    camera.setOrthographic(true);
}

void Application::storeSelectionSet(int slot) {
    m_ctx->selectionSets[slot] = m_ctx->selection.items();
    m_ctx->toast(std::format("Selection set {} stored ({} entities)", slot, m_ctx->selection.size()), ToastKind::Success);
}

void Application::recallSelectionSet(int slot) {
    std::vector<EntityHandle> alive;
    for (EntityHandle e : m_ctx->selectionSets[slot]) {
        const EntityHandle resolved = m_ctx->history->resolve(e);
        if (m_ctx->world.isAlive(resolved)) alive.push_back(resolved);
    }
    if (alive.empty()) {
        m_ctx->toast(std::format("Selection set {} is empty. Ctrl+{} stores one", slot, slot), ToastKind::Warning);
        return;
    }
    m_ctx->selection.setAll(alive);
    if (ImGui::GetIO().KeyShift) frameSelection();
}

void Application::storeBookmark(int slot) {
    const Camera& camera = m_ctx->world.camera();
    m_ctx->bookmarks[slot] = {camera.getPosition(), camera.getYaw(), camera.getPitch(), true};
    m_ctx->toast(std::format("Camera view {} stored", slot), ToastKind::Success);
}

void Application::recallBookmark(int slot) {
    const CameraBookmark& b = m_ctx->bookmarks[slot];
    if (!b.valid) {
        m_ctx->toast(std::format("No camera view {}. Ctrl+Alt+{} stores one", slot, slot), ToastKind::Warning);
        return;
    }
    const Camera& camera = m_ctx->world.camera();
    CameraFlight& f = m_ctx->flight;
    f.active = true;
    f.t = 0.0f;
    f.duration = 0.35f;
    f.from = camera.getPosition();
    f.to = b.position;
    f.fromYaw = camera.getYaw();
    f.toYaw = b.yaw;
    f.fromPitch = camera.getPitch();
    f.toPitch = b.pitch;
    f.fromOrtho = f.toOrtho = 0.0f;
    m_ctx->world.camera().setOrthographic(false);
}

void Application::selectSimilar(bool byMaterial) {
    EditorContext& ctx = *m_ctx;
    const EntityHandle primary = ctx.primary();
    if (!ctx.world.isAlive(primary)) return;
    const uint32_t mesh = ctx.world.getMesh(primary);
    const uint32_t material = ctx.world.getRenderable(primary).material_uuid;
    constexpr size_t kLimit = 100000;
    std::vector<EntityHandle> found;
    ctx.world.forEach([&](EntityHandle e) {
        if (found.size() >= kLimit) return;
        if (byMaterial ? ctx.world.getRenderable(e).material_uuid == material : ctx.world.getMesh(e) == mesh) found.push_back(e);
    });
    ctx.selection.setAll(found);
    ctx.selection.remove(primary);
    ctx.selection.add(primary);
    ctx.toast(std::format("Selected {} with the same {}{}", found.size(), byMaterial ? "material" : "mesh", found.size() >= kLimit ? " (capped)" : ""));
}

void Application::registerCommands() {
    EditorContext& ctx = *m_ctx;
    CommandRegistry& c = m_commands;
    auto hasSelection = [&ctx] { return !ctx.selection.empty(); };
    auto add = [&](std::string id, std::string title, std::string category, std::vector<Chord> chords, std::function<void()> run, std::function<bool()> enabled = {}, CommandScope scope = CommandScope::Viewport) -> Command& {
        Command command;
        command.id = std::move(id);
        command.title = std::move(title);
        command.category = std::move(category);
        command.chords = std::move(chords);
        command.run = std::move(run);
        command.enabled = std::move(enabled);
        command.scope = scope;
        return c.add(std::move(command));
    };

    add("file.save", "Save scene", "File", {ctrl(ImGuiKey_S)}, [this] { saveScene(m_ctx->scenePath); }, {}, CommandScope::Global);
    add("file.saveAs", "Save scene as...", "File", {ctrlShift(ImGuiKey_S)}, [this] {
        m_palette.prompt({"Save scene as", m_ctx->scenePath, "resources/level.litscene", sceneFiles(), [this](const std::string& path) { saveScene(path); }});
    }, {}, CommandScope::Global);
    add("file.open", "Open scene...", "File", {ctrl(ImGuiKey_O)}, [this] {
        m_palette.prompt({"Open scene", "", "type or pick a .litscene", sceneFiles(), [this](const std::string& path) { guarded([this, path] { loadScene(path); }); }});
    }, {}, CommandScope::Global);
    add("file.new", "New empty scene", "File", {ctrl(ImGuiKey_N)}, [this] {
        m_palette.prompt({std::format("Clear all {} entities? This cannot be undone. Type yes", m_ctx->world.aliveCount()), "", "yes", {}, [this](const std::string& answer) {
                              if (answer != "yes") return;
                              if (m_ctx->playing) setPlaying(false);
                              m_ctx->world.clear();
                              m_engine.setAnimation(0.0f, 0, 1);
                              m_movingObjectCount = 0;
                              afterSceneReplaced();
                              m_ctx->scenePath = "resources/untitled.litscene";
                              m_ctx->toast("New scene", ToastKind::Success);
                          }});
    }, {}, CommandScope::Global);
    add("file.quit", "Quit", "File", {ctrl(ImGuiKey_Q)}, [this] { guarded([this] { m_ctx->requestQuit = true; }); }, {}, CommandScope::Global);

    add("edit.undo", "Undo", "Edit", {ctrl(ImGuiKey_Z)}, [this] {
        if (!m_ctx->history->undo()) m_ctx->toast("Nothing to undo");
        m_ctx->selection.prune(m_ctx->world);
    }, {}, CommandScope::Global).repeat = true;
    add("edit.redo", "Redo", "Edit", {ctrlShift(ImGuiKey_Z), ctrl(ImGuiKey_Y)}, [this] {
        if (!m_ctx->history->redo()) m_ctx->toast("Nothing to redo");
        m_ctx->selection.prune(m_ctx->world);
    }, {}, CommandScope::Global).repeat = true;
    add("edit.duplicate", "Duplicate and move", "Edit", {shift(ImGuiKey_D)}, [this] { duplicateSelection(true); }, hasSelection);
    add("edit.duplicateInPlace", "Duplicate in place", "Edit", {ctrl(ImGuiKey_D)}, [this] { duplicateSelection(false); }, hasSelection);
    add("edit.delete", "Delete", "Edit", {key(ImGuiKey_Delete)}, [this] { deleteSelection(); }, hasSelection);
    add("edit.copy", "Copy", "Edit", {ctrl(ImGuiKey_C)}, [this] { copySelection(); }, hasSelection);
    add("edit.cut", "Cut", "Edit", {ctrl(ImGuiKey_X)}, [this] {
        copySelection();
        deleteSelection();
    }, hasSelection);
    add("edit.paste", "Paste at cursor", "Edit", {ctrl(ImGuiKey_V)}, [this] { paste(); }, [this] { return !m_ctx->clipboard.empty(); });
    add("edit.rename", "Rename", "Edit", {key(ImGuiKey_F2)}, [this] {
        m_ctx->showInspector = true;
        m_inspector.focusName();
    }, hasSelection);
    add("edit.group", "Group under new parent", "Edit", {ctrl(ImGuiKey_G)}, [this] { groupSelection(); }, hasSelection);
    add("edit.parent", "Parent selection to active", "Edit", {key(ImGuiKey_P)}, [this] { parentToActive(); }, hasSelection);
    add("edit.unparent", "Clear parent", "Edit", {alt(ImGuiKey_P)}, [this] {
        m_ctx->history->beginGroup("Unparent");
        for (EntityHandle e : m_ctx->topLevelSelection()) m_ctx->history->setParent(e, NULL_ENTITY);
        m_ctx->history->endGroup();
    }, hasSelection);
    add("material.assign", "Assign material...", "Material", {key(ImGuiKey_M)}, [this] {
        std::vector<std::string> names;
        for (uint32_t id = 0; id < m_engine.materialCount(); ++id) names.push_back(std::format("{}  #{}", m_engine.material(id).name, id));
        m_palette.prompt({"Assign material", "", "material name or #id", names, [this](const std::string& picked) {
                              const size_t hash = picked.rfind('#');
                              if (hash == std::string::npos) return;
                              const uint32_t id = static_cast<uint32_t>(std::atoi(picked.c_str() + hash + 1));
                              if (id >= m_engine.materialCount()) return;
                              m_ctx->history->beginGroup("Assign material");
                              for (EntityHandle e : m_ctx->topLevelSelection()) {
                                  const RenderableComponent r = m_ctx->world.getRenderable(e);
                                  m_ctx->history->setRender(e, m_ctx->world.getMesh(e), id, r.shaderId, r.alpha);
                              }
                              m_ctx->history->endGroup();
                          }});
    }, hasSelection);
    add("material.new", "New material for selection", "Material", {shift(ImGuiKey_M)}, [this] {
        Material m = m_engine.material(m_ctx->world.getRenderable(m_ctx->primary()).material_uuid);
        m.name = std::format("Material {}", m_engine.materialCount());
        const uint32_t id = m_engine.createMaterial(m);
        if (id == 0) return;
        m_ctx->history->beginGroup("New material");
        for (EntityHandle e : m_ctx->topLevelSelection()) {
            const RenderableComponent r = m_ctx->world.getRenderable(e);
            m_ctx->history->setRender(e, m_ctx->world.getMesh(e), id, r.shaderId, r.alpha);
        }
        m_ctx->history->endGroup();
        m_ctx->showInspector = true;
    }, hasSelection);

    add("select.all", "Select all", "Select", {ctrl(ImGuiKey_A)}, [this] {
        constexpr size_t kLimit = 100000;
        std::vector<EntityHandle> all;
        m_ctx->world.forEach([&](EntityHandle e) {
            if (all.size() < kLimit) all.push_back(e);
        });
        m_ctx->selection.setAll(all);
        if (m_ctx->world.aliveCount() > kLimit) m_ctx->toast(std::format("Selected the first {} of {}", kLimit, m_ctx->world.aliveCount()), ToastKind::Warning);
    });
    add("select.none", "Select none", "Select", {alt(ImGuiKey_A), key(ImGuiKey_Escape)}, [this] { m_ctx->selection.clear(); }, hasSelection);
    add("select.parent", "Select parent", "Select", {shift(ImGuiKey_LeftBracket)}, [this] { walk(2); }, hasSelection);
    add("select.child", "Select first child", "Select", {shift(ImGuiKey_RightBracket)}, [this] { walk(-2); }, hasSelection);
    add("select.next", "Select next sibling", "Select", {key(ImGuiKey_RightBracket)}, [this] { walk(1); }).repeat = true;
    add("select.previous", "Select previous sibling", "Select", {key(ImGuiKey_LeftBracket)}, [this] { walk(-1); }).repeat = true;
    add("select.children", "Select children", "Select", {ctrl(ImGuiKey_RightBracket)}, [this] {
        std::vector<EntityHandle> children;
        for (EntityHandle e : m_ctx->selection.items()) {
            for (EntityHandle child : m_ctx->world.getChildren(e)) {
                if (children.size() < 100000) children.push_back(child);
            }
        }
        if (!children.empty()) m_ctx->selection.setAll(children);
    }, hasSelection);
    add("select.sameMesh", "Select same mesh", "Select", {ctrl(ImGuiKey_L)}, [this] { selectSimilar(false); }, hasSelection);
    add("select.sameMaterial", "Select same material", "Select", {ctrlShift(ImGuiKey_L)}, [this] { selectSimilar(true); }, hasSelection);
    add("select.find", "Find entity...", "Select", {ctrl(ImGuiKey_F)}, [this] {
        m_ctx->openPalette = true;
        m_ctx->paletteSeed = "@";
    }, {}, CommandScope::Global);
    const ImGuiKey digits[10] = {ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
    for (int slot = 1; slot <= 9; ++slot) {
        const bool first = slot == 1;
        auto& store = add(std::format("sets.store{}", slot), first ? "Store selection set 1-9" : std::format("Store selection set {}", slot), "Selection sets", {ctrl(digits[slot])}, [this, slot] { storeSelectionSet(slot); }, hasSelection);
        auto setup = [first](Command& command, const char* keys) {
            command.palette = first;
            command.help = first;
            if (first) command.keysLabel = keys;
        };
        setup(store, "Ctrl+1..9");
        setup(add(std::format("sets.recall{}", slot), first ? "Recall selection set 1-9 (Shift also frames)" : std::format("Recall selection set {}", slot), "Selection sets", {key(digits[slot]), shift(digits[slot])}, [this, slot] { recallSelectionSet(slot); }), "1..9");
        setup(add(std::format("view.store{}", slot), first ? "Store camera view 1-9" : std::format("Store camera view {}", slot), "Camera views", {ctrlAlt(digits[slot])}, [this, slot] { storeBookmark(slot); }), "Ctrl+Alt+1..9");
        setup(add(std::format("view.recall{}", slot), first ? "Recall camera view 1-9" : std::format("Recall camera view {}", slot), "Camera views", {alt(digits[slot])}, [this, slot] { recallBookmark(slot); }), "Alt+1..9");
    }

    add("transform.move", "Move (type X/Y/Z and a value)", "Transform", {key(ImGuiKey_G)}, [this] { m_tool.begin(*m_ctx, ModalKind::Move); }, hasSelection);
    add("transform.rotate", "Rotate", "Transform", {key(ImGuiKey_R)}, [this] { m_tool.begin(*m_ctx, ModalKind::Rotate); }, hasSelection);
    add("transform.scale", "Scale", "Transform", {key(ImGuiKey_S)}, [this] { m_tool.begin(*m_ctx, ModalKind::Scale); }, hasSelection);
    add("transform.handles", "Cycle handles: move / rotate / scale / off", "Transform", {key(ImGuiKey_Q)}, [this] {
        m_ctx->handles = static_cast<HandleMode>((static_cast<int>(m_ctx->handles) + 1) % 4);
        const char* names[] = {"Move handles", "Rotate handles", "Scale handles", "Handles off"};
        m_ctx->toast(names[static_cast<int>(m_ctx->handles)]);
    });
    add("transform.space", "Toggle local / world space", "Transform", {key(ImGuiKey_GraveAccent)}, [this] {
        m_ctx->localSpace = !m_ctx->localSpace;
        m_ctx->toast(m_ctx->localSpace ? "Local space" : "World space");
    }).checked = [this] { return m_ctx->localSpace; };
    add("transform.snap", "Toggle snapping", "Transform", {shift(ImGuiKey_Tab)}, [this] {
        m_ctx->snapping = !m_ctx->snapping;
        m_ctx->toast(m_ctx->snapping ? std::format("Snapping on ({} m, {}°)", m_ctx->snapMove, m_ctx->snapAngle) : "Snapping off");
    }).checked = [this] { return m_ctx->snapping; };
    add("transform.resetPosition", "Reset position", "Transform", {alt(ImGuiKey_G)}, [this] { m_tool.resetTransform(*m_ctx, 0); }, hasSelection);
    add("transform.resetRotation", "Reset rotation", "Transform", {alt(ImGuiKey_R)}, [this] { m_tool.resetTransform(*m_ctx, 1); }, hasSelection);
    add("transform.resetScale", "Reset scale", "Transform", {alt(ImGuiKey_S)}, [this] { m_tool.resetTransform(*m_ctx, 2); }, hasSelection);
    add("transform.toCursor", "Move selection to cursor", "Transform", {key(ImGuiKey_T)}, [this] {
        const auto top = m_ctx->topLevelSelection();
        if (top.empty()) return;
        glm::vec3 center(0.0f);
        for (EntityHandle e : top) center += m_ctx->world.getWorldPosition(e);
        center /= static_cast<float>(top.size());
        const glm::vec3 delta = m_ctx->cursor - center;
        m_ctx->history->beginGroup("Move to cursor");
        for (EntityHandle e : top) {
            const EntityHandle parent = m_ctx->world.getParent(e);
            const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : m_ctx->world.getWorldMatrix(parent);
            m_ctx->history->setLocalMatrix(e, glm::inverse(parentWorld) * glm::translate(glm::mat4(1.0f), delta) * m_ctx->world.getWorldMatrix(e));
        }
        m_ctx->history->endGroup();
    }, hasSelection);
    add("transform.drop", "Drop to ground", "Transform", {key(ImGuiKey_End)}, [this] {
        m_ctx->history->beginGroup("Drop to ground");
        for (EntityHandle e : m_ctx->topLevelSelection()) {
            const glm::vec4 bounds = m_ctx->world.getWorldBounds(e);
            const glm::vec3 p = m_ctx->world.getWorldPosition(e);
            const float lift = bounds.w > 0.0f ? p.y - bounds.y + bounds.w * 0.58f : 0.0f;
            const EntityHandle parent = m_ctx->world.getParent(e);
            const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : m_ctx->world.getWorldMatrix(parent);
            glm::mat4 world = m_ctx->world.getWorldMatrix(e);
            world[3].y = lift;
            m_ctx->history->setLocalMatrix(e, glm::inverse(parentWorld) * world);
        }
        m_ctx->history->endGroup();
    }, hasSelection);
    struct Nudge {
        ImGuiKey key;
        glm::vec3 dir;
        const char* name;
    };
    const Nudge nudges[] = {{ImGuiKey_LeftArrow, glm::vec3(-1, 0, 0), "left"}, {ImGuiKey_RightArrow, glm::vec3(1, 0, 0), "right"}, {ImGuiKey_UpArrow, glm::vec3(0, 0, 1), "away"}, {ImGuiKey_DownArrow, glm::vec3(0, 0, -1), "closer"}, {ImGuiKey_PageUp, glm::vec3(0, 1, 0), "up"}, {ImGuiKey_PageDown, glm::vec3(0, -1, 0), "down"}};
    for (const Nudge& n : nudges) {
        const glm::vec3 dir = n.dir;
        auto& command = add(std::format("transform.nudge.{}", n.name), std::format("Nudge {} (Shift x10)", n.name), "Transform", {key(n.key), shift(n.key)}, [this, dir] { m_tool.nudge(*m_ctx, dir); }, hasSelection);
        command.repeat = true;
        command.palette = false;
        command.enabled = [this] { return !m_ctx->selection.empty() && !m_ctx->outlinerFocused; };
        command.help = n.key == ImGuiKey_LeftArrow;
        if (command.help) {
            command.title = "Nudge along the ground (PgUp/PgDn vertical, Shift x10)";
            command.keysLabel = "Arrows";
        }
    }

    add("view.focus", "Frame selection", "View", {key(ImGuiKey_F)}, [this] { frameSelection(); });
    add("view.frameAll", "Frame everything", "View", {key(ImGuiKey_Home), shift(ImGuiKey_F)}, [this] { frameAll(); });
    add("view.top", "View from top", "View", {key(ImGuiKey_Keypad7)}, [this] { setView(glm::vec3(0.0f, -1.0f, 0.0f)); });
    add("view.bottom", "View from bottom", "View", {ctrl(ImGuiKey_Keypad7)}, [this] { setView(glm::vec3(0.0f, 1.0f, 0.0f)); });
    add("view.front", "View from front", "View", {key(ImGuiKey_Keypad1)}, [this] { setView(glm::vec3(0.0f, 0.0f, -1.0f)); });
    add("view.back", "View from back", "View", {ctrl(ImGuiKey_Keypad1)}, [this] { setView(glm::vec3(0.0f, 0.0f, 1.0f)); });
    add("view.ortho", "Toggle orthographic / perspective", "View", {key(ImGuiKey_Keypad5)}, [this] { toggleOrthographic(); }).checked = [this] { return m_ctx->world.camera().isOrthographic(); };
    add("view.right", "View from right", "View", {key(ImGuiKey_Keypad3)}, [this] { setView(glm::vec3(-1.0f, 0.0f, 0.0f)); });
    add("view.left", "View from left", "View", {ctrl(ImGuiKey_Keypad3)}, [this] { setView(glm::vec3(1.0f, 0.0f, 0.0f)); });
    add("view.zen", "Zen mode (hide all panels)", "View", {key(ImGuiKey_Tab)}, [this] { m_ctx->zen = !m_ctx->zen; }).checked = [this] { return m_ctx->zen; };
    add("view.outliner", "Scene outliner", "View", {key(ImGuiKey_O)}, [this] { m_ctx->showOutliner = !m_ctx->showOutliner; }).checked = [this] { return m_ctx->showOutliner; };
    add("view.search", "Filter the outliner", "View", {ctrlShift(ImGuiKey_F)}, [this] {
        m_ctx->showOutliner = true;
        m_outliner.focusSearch();
    }, {}, CommandScope::Global);
    add("view.inspector", "Inspector card", "View", {key(ImGuiKey_I)}, [this] { m_ctx->showInspector = !m_ctx->showInspector; }).checked = [this] { return m_ctx->showInspector; };
    add("view.pinInspector", "Pin / float the inspector card", "View", {ctrlShift(ImGuiKey_I)}, [this] {
        m_ctx->pinInspector = !m_ctx->pinInspector;
        if (m_ctx->pinInspector) m_inspector.dock();
        m_ctx->toast(m_ctx->pinInspector ? "Card pinned" : "Card follows the selection");
    }, {}, CommandScope::Global).checked = [this] { return m_ctx->pinInspector; };
    add("view.world", "World: sky, sun, exposure, fog", "View", {shift(ImGuiKey_W)}, [this] {
        const bool show = !m_look.worldVisible();
        m_ctx->showWorld = show && !m_ctx->selection.empty();
        m_ctx->worldDismissed = !show;
        if (show && m_ctx->showLook && m_ctx->selection.empty()) m_ctx->showWorld = true;
    }).checked = [this] { return m_look.worldVisible(); };
    add("view.look", "Look: rendering and lighting", "View", {key(ImGuiKey_L)}, [this] { m_ctx->showLook = !m_ctx->showLook; }).checked = [this] { return m_ctx->showLook; };
    add("view.stats", "Stats overlay", "View", {ctrlShift(ImGuiKey_T)}, [this] { m_ctx->showStats = !m_ctx->showStats; }, {}, CommandScope::Global).checked = [this] { return m_ctx->showStats; };
    add("view.grid", "Grid", "View", {ctrlShift(ImGuiKey_G)}, [this] { m_ctx->showGrid = !m_ctx->showGrid; }).checked = [this] { return m_ctx->showGrid; };
    add("view.timeline", "History timeline", "View", {ctrl(ImGuiKey_H)}, [this] { m_ctx->showTimeline = !m_ctx->showTimeline; }, {}, CommandScope::Global).checked = [this] { return m_ctx->showTimeline; };
    add("view.help", "Keyboard map", "View", {key(ImGuiKey_F1), shift(ImGuiKey_Slash)}, [this] { m_ctx->showHelp = !m_ctx->showHelp; }, {}, CommandScope::Global);
    add("view.palette", "Command palette", "View", {ctrl(ImGuiKey_P), ctrlShift(ImGuiKey_P)}, [this] {
        m_ctx->openPalette = true;
        m_ctx->paletteSeed = ImGui::GetIO().KeyShift ? ">" : "";
    }, {}, CommandScope::Global).palette = false;
    add("view.calculator", "Calculator", "View", {key(ImGuiKey_Equal)}, [this] {
        m_ctx->openPalette = true;
        m_ctx->paletteSeed = "=";
    });
    add("view.flyFaster", "Faster camera", "View", {key(ImGuiKey_KeypadAdd)}, [this] { m_ctx->flySpeed = std::min(m_ctx->flySpeed * 1.5f, 5000.0f); });
    add("view.flySlower", "Slower camera", "View", {key(ImGuiKey_KeypadSubtract)}, [this] { m_ctx->flySpeed = std::max(m_ctx->flySpeed / 1.5f, 0.1f); });

    add("create.menu", "Add...", "Create", {shift(ImGuiKey_A)}, [this] { m_ctx->openQuickAdd = true; });
    for (const char* kind : {"Cube", "Sphere", "Empty", "Point light", "Spot light", "Sun light", "Camera"}) {
        const std::string name = kind;
        std::string id = "create.";
        for (char ch : name) {
            if (ch != ' ') id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        add(id, "Add " + name, "Create", {}, [this, name] { createPrimitive(name); });
    }

    add("hide.selected", "Hide / show selection", "Visibility", {key(ImGuiKey_H)}, [this] {
        const bool visible = !m_ctx->world.isVisible(m_ctx->primary());
        m_ctx->history->beginGroup(visible ? "Show" : "Hide");
        for (EntityHandle e : m_ctx->topLevelSelection()) m_ctx->history->setVisible(e, visible);
        m_ctx->history->endGroup();
    }, hasSelection);
    add("hide.revealAll", "Show everything hidden", "Visibility", {alt(ImGuiKey_H)}, [this] {
        World& world = m_ctx->world;
        auto hidden = std::make_shared<std::vector<EntityHandle>>();
        world.forEach([&](EntityHandle e) {
            if (!world.isVisible(e)) hidden->push_back(e);
        });
        const size_t shown = hidden->size();
        if (shown > 0) {
            auto apply = [&world, hidden](bool visible) {
                for (EntityHandle e : *hidden) world.setVisible(e, visible);
            };
            apply(true);
            m_ctx->history->record("Show all", [apply] { apply(true); }, [apply] { apply(false); });
        }
        m_ctx->toast(std::format("Revealed {}", shown));
    });

    add("play.toggle", "Play / stop", "Play", {key(ImGuiKey_F5)}, [this] { setPlaying(!m_ctx->playing); }, {}, CommandScope::Global);
    add("play.pause", "Pause / resume", "Play", {key(ImGuiKey_F6)}, [this] {
        m_ctx->paused = !m_ctx->paused;
        m_engine.setPaused(m_ctx->paused);
    }, [this] { return m_ctx->playing; }, CommandScope::Global);
    add("play.step", "Step one frame", "Play", {key(ImGuiKey_F7)}, [this] { m_stepOnce = true; }, [this] { return m_ctx->playing && m_ctx->paused; }, CommandScope::Global);

    add("scene.showcase", "Load the PBR showcase scene", "File", {}, [this] {
        if (m_ctx->playing) setPlaying(false);
        m_ctx->world.clear();
        m_engine.portals().clear();
        m_engine.setAnimation(0.0f, 0, 1);
        buildShowcaseScene();
        afterSceneReplaced();
        m_ctx->toast("Showcase scene", ToastKind::Success);
    });
    add("scene.portalLoop", "Load the portal loop test scene", "File", {}, [this] {
        if (m_ctx->playing) setPlaying(false);
        m_ctx->world.clear();
        m_engine.setAnimation(0.0f, 0, 1);
        buildPortalScene(0);
        afterSceneReplaced();
        m_ctx->toast("Portal loop scene", ToastKind::Success);
    });
    add("scene.portalTorture", "Load the nested portal torture scene", "File", {}, [this] {
        if (m_ctx->playing) setPlaying(false);
        m_ctx->world.clear();
        m_engine.setAnimation(0.0f, 0, 1);
        buildPortalScene(1);
        afterSceneReplaced();
        m_ctx->toast("Portal torture scene", ToastKind::Success);
    });
    add("render.reload", "Reload shaders", "Render", {key(ImGuiKey_F9)}, [this] {
        m_engine.reloadShaders();
        m_ctx->toast("Shaders reloaded");
    }, {}, CommandScope::Global);
    add("render.depth", "Hi-Z depth view", "Render", {}, [this] {
        m_lookState.debugDepth = !m_lookState.debugDepth;
        m_engine.setDebugDepthMode(m_lookState.debugDepth);
    }).checked = [this] { return m_lookState.debugDepth; };
    add("render.lodUp", "Force a coarser LOD", "Render", {}, [this] {
        m_lookState.forcedLod = std::min(m_lookState.forcedLod + 1, static_cast<int>(kLodLevelCount) - 1);
        m_engine.setForcedLod(m_lookState.forcedLod);
    });
    add("render.lodAuto", "Automatic LOD", "Render", {}, [this] {
        m_lookState.forcedLod = -1;
        m_engine.setForcedLod(-1);
    });
    add("render.animate", "Animate stress objects", "Render", {}, [this] { m_lookState.animateStress = !m_lookState.animateStress; }).checked = [this] { return m_lookState.animateStress; };
    {
        const char* views[] = {"Lit", "Base color", "Normals", "Roughness", "Metallic", "Ambient occlusion", "Sun shadows", "Reflections", "Lighting only", "Luminance heatmap", "Light clusters"};
        for (int v = 0; v < 11; ++v) {
            add(std::format("debug.view{}", v), std::format("Debug view: {}", views[v]), "Render", {}, [this, v] { m_engine.renderSettings().debugView = static_cast<DebugView>(v); }).checked = [this, v] { return static_cast<int>(m_engine.renderSettings().debugView) == v; };
        }
        add("debug.cycle", "Cycle debug view", "Render", {alt(ImGuiKey_L)}, [this, views] {
            RenderSettings& r = m_engine.renderSettings();
            r.debugView = static_cast<DebugView>((static_cast<int>(r.debugView) + 1) % 11);
            m_ctx->toast(std::format("View: {}", views[static_cast<int>(r.debugView)]));
        });
        const std::pair<const char*, float> times[] = {{"Sunrise", 6.4f}, {"Morning", 9.0f}, {"Noon", 12.5f}, {"Golden hour", 17.4f}, {"Sunset", 18.3f}, {"Night", 22.5f}};
        for (const auto& [name, hour] : times) {
            const float h = hour;
            add(std::format("time.{}", name), std::format("Time of day: {}", name), "Lighting", {}, [this, h] { m_engine.renderSettings().timeOfDay = h; });
        }
        add("time.animate", "Animate time of day", "Lighting", {ctrlShift(ImGuiKey_D)}, [this] { m_engine.renderSettings().animateTime = !m_engine.renderSettings().animateTime; }, {}, CommandScope::Global).checked = [this] { return m_engine.renderSettings().animateTime; };
        add("time.later", "Sun later (+30 min)", "Lighting", {ctrl(ImGuiKey_RightArrow)}, [this] {
            RenderSettings& r = m_engine.renderSettings();
            r.timeOfDay = std::fmod(r.timeOfDay + 0.5f, 24.0f);
        }).repeat = true;
        add("time.earlier", "Sun earlier (-30 min)", "Lighting", {ctrl(ImGuiKey_LeftArrow)}, [this] {
            RenderSettings& r = m_engine.renderSettings();
            r.timeOfDay = std::fmod(r.timeOfDay + 23.5f, 24.0f);
        }).repeat = true;
        add("render.tonemapper", "Cycle tonemapper", "Render", {}, [this] {
            RenderSettings& r = m_engine.renderSettings();
            r.tonemapper = static_cast<Tonemapper>((static_cast<int>(r.tonemapper) + 1) % 6);
            const char* names[] = {"AgX", "AgX punchy", "ACES", "PBR Neutral", "Reinhard", "Linear"};
            m_ctx->toast(std::format("Tonemapper: {}", names[static_cast<int>(r.tonemapper)]));
        });
        auto toggle = [&](const char* id, const char* title, bool RenderSettings::*member) {
            add(id, title, "Render", {}, [this, member] { m_engine.renderSettings().*member = !(m_engine.renderSettings().*member); }).checked = [this, member] { return m_engine.renderSettings().*member; };
        };
        toggle("render.shadows", "Shadows", &RenderSettings::shadows);
        toggle("render.ssao", "Ambient occlusion", &RenderSettings::ssao);
        toggle("render.ssr", "Screen-space reflections", &RenderSettings::ssr);
        toggle("render.bloom", "Bloom", &RenderSettings::bloom);
        toggle("render.fxaa", "FXAA", &RenderSettings::fxaa);
        toggle("render.clouds", "Clouds", &RenderSettings::clouds);
        toggle("render.fog", "Fog and aerial perspective", &RenderSettings::fog);
        toggle("render.autoExposure", "Auto exposure", &RenderSettings::autoExposure);
    }
    for (const PassInfo& pass : m_engine.customPasses()) {
        const PassKind kind = pass.kind;
        const uint32_t id = pass.id;
        const std::string title = std::format("{}: {}", kind == PassKind::Effect ? "Effect" : kind == PassKind::Draw ? "Draw" : "Compute", pass.name);
        add(std::format("pass.{}", id), "Toggle " + title, "Shaders", {}, [this, kind, id] {
            for (const PassInfo& p : m_engine.customPasses()) {
                if (p.id == id) m_engine.setPassEnabled(kind, id, !p.enabled);
            }
        }).checked = [this, id] {
            for (const PassInfo& p : m_engine.customPasses()) {
                if (p.id == id) return p.enabled;
            }
            return false;
        };
    }
    add("shader.new", "New surface shader for selection...", "Shaders", {}, [this] {
        m_palette.prompt({"New surface shader name (or path to a .surface file)", "", "e.g. rusty", {}, [this](const std::string& name) {
                              const std::filesystem::path given(name);
                              const bool bare = !given.has_parent_path();
                              std::string clean;
                              for (char ch : given.extension() == ".surface" ? given.stem().string() : name) {
                                  if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                              }
                              if (clean.empty()) {
                                  m_ctx->toast(std::format("'{}' is not a valid shader name", name), ToastKind::Error);
                                  return;
                              }
                              std::filesystem::path path = bare ? std::filesystem::path("resources/shaders/surfaces") / (clean + ".surface") : given;
#ifdef LIT_SOURCE_DIR
                              if (bare && std::filesystem::is_directory(LIT_SOURCE_DIR "/resources/shaders/surfaces")) path = std::filesystem::path(LIT_SOURCE_DIR "/resources/shaders/surfaces") / (clean + ".surface");
                              if (!bare && given.is_relative() && !std::filesystem::exists(path) && std::filesystem::exists(std::filesystem::path(LIT_SOURCE_DIR) / given)) path = std::filesystem::path(LIT_SOURCE_DIR) / given;
#endif
                              if (!std::filesystem::exists(path)) {
                                  std::error_code error;
                                  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
                                  std::ofstream out(path);
                                  out << "void vertex(inout VertexData v, in VertexContext c) {\n}\n\nvoid surface(in SurfaceInput i, inout Surface s) {\n    s.baseColor *= 0.5 + 0.5 * litNoise(i.worldPosition * 4.0);\n}\n";
                                  if (!out.good()) {
                                      m_ctx->toast(std::format("Cannot create {}", path.string()), ToastKind::Error);
                                      return;
                                  }
                                  m_ctx->toast(std::format("Created {} from the template", path.string()));
                              } else if (!std::filesystem::is_regular_file(path)) {
                                  m_ctx->toast(std::format("{} is not a file", path.string()), ToastKind::Error);
                                  return;
                              }
                              const uint32_t id = m_engine.loadShader(clean, path.string());
                              m_ctx->history->beginGroup("Assign shader");
                              for (EntityHandle e : m_ctx->topLevelSelection()) {
                                  const RenderableComponent r = m_ctx->world.getRenderable(e);
                                  m_ctx->history->setRender(e, m_ctx->world.getMesh(e), r.material_uuid, id, r.alpha);
                              }
                              m_ctx->history->endGroup();
                              if (m_engine.shaderStatus(clean) == "failed") return;
                              m_ctx->toast(std::format("Edit {} and save: it hot-reloads", path.string()), ToastKind::Success);
                          }});
    });
    add("profile.cpu", "CPU profiler", "Profile", {key(ImGuiKey_F10)}, [this] { m_engine.profiler().setEnabled(!m_engine.profiler().enabled()); }, {}, CommandScope::Global).checked = [this] { return m_engine.profiler().enabled(); };
    add("profile.trace", "Write Chrome trace", "Profile", {key(ImGuiKey_F11)}, [this] {
        if (m_engine.profiler().writeChromeTrace("resources/trace.json")) m_ctx->toast("Trace written to resources/trace.json", ToastKind::Success);
    }, {}, CommandScope::Global);
}

void Application::drawQuickAdd() {
    EditorContext& ctx = *m_ctx;
    if (ctx.openQuickAdd) {
        ImGui::OpenPopup("Add");
        ctx.openQuickAdd = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetIO().MousePos, ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Add")) {
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
        const struct {
            ImGuiKey key;
            const char* kind;
        } accelerators[] = {{ImGuiKey_C, "Cube"}, {ImGuiKey_S, "Sphere"}, {ImGuiKey_E, "Empty"}, {ImGuiKey_P, "Point light"}, {ImGuiKey_O, "Spot light"}, {ImGuiKey_U, "Sun light"}, {ImGuiKey_A, "Camera"}};
        const ImGuiIO& io = ImGui::GetIO();
        const bool acceptKeys = !ImGui::IsWindowAppearing() && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
        for (const auto& a : accelerators) {
            if (acceptKeys && ImGui::IsKeyPressed(a.key, false)) {
                createPrimitive(a.kind);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void Application::drawRecovery() {
    if (!m_recoveryPending) return;
    ImGui::OpenPopup("Recover?");
    if (ImGui::BeginPopupModal("Recover?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("An autosave from a previous session was found.");
        ImGui::TextDisabled("%s", kAutosavePath);
        ImGui::Spacing();
        if (ImGui::Button("Recover  (Y)", ImVec2(150.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Y)) {
            loadScene(kAutosavePath);
            m_recoveryPending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard  (N)", ImVec2(150.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_N) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            std::error_code ec;
            std::filesystem::remove(kAutosavePath, ec);
            m_recoveryPending = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::drawStats(float frameMs) {
    EditorContext& ctx = *m_ctx;
    if (!ctx.showStats || ctx.zen) return;
    const glm::vec2 size = m_engine.windowSize();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const Camera& camera = ctx.world.camera();
    const glm::vec3 p = camera.getPosition();
    size_t lights = 0;
    ctx.world.view<LightComponent>([&lights](EntityHandle, LightComponent&) { ++lights; });
    const std::string lines[] = {
        std::format("{:.1f} fps  {:.2f} ms", 1000.0f / std::max(frameMs, 0.01f), frameMs),
        std::format("gpu {:.2f} ms", m_engine.gpuTotalMilliseconds()),
        std::format("{} entities  {} lights  {} selected", ctx.world.aliveCount(), lights, ctx.selection.size()),
        std::format("cam {:.0f} {:.0f} {:.0f}  {}", p.x, p.y, p.z, camera.isOrthographic() ? "ortho" : "persp"),
        std::format("speed {:.1f}  {}{}", ctx.flySpeed, ctx.snapping ? "snap  " : "", ctx.localSpace ? "local" : "world"),
    };
    float y = ImGui::GetMainViewport()->WorkPos.y + 122.0f;
    for (const std::string& line : lines) {
        const ImVec2 textSize = ImGui::CalcTextSize(line.c_str());
        draw->AddText(ImVec2(size.x - textSize.x - 16.0f, y), IM_COL32(200, 205, 214, 200), line.c_str());
        y += textSize.y + 2.0f;
    }
}

void Application::updateTitle() {
    const std::string title = std::format("Lit Editor  ·  {}{}{}", m_ctx->scenePath, m_ctx->dirty() ? " *" : "", m_ctx->playing ? "  [playing]" : "");
    if (title != m_lastTitle) {
        glfwSetWindowTitle(m_window, title.c_str());
        m_lastTitle = title;
    }
}

void Application::update() {
    EditorContext& ctx = *m_ctx;
    if (glfwWindowShouldClose(m_window)) {
        glfwSetWindowShouldClose(m_window, GLFW_FALSE);
        guarded([this] { m_ctx->requestQuit = true; });
    }
    const double now = glfwGetTime();
    const float deltaTime = static_cast<float>(std::min(now - m_lastFrame, 0.25));
    m_lastFrame = now;
    m_frameMs = deltaTime * 1000.0f;

    m_autopilot.beforeFrame(ctx, m_commands, deltaTime);
    for (const ShaderError& error : m_engine.takeShaderErrors()) {
        const std::string file = std::filesystem::path(error.source).filename().string();
        std::string message = error.message;
        for (size_t at = 0; (at = message.find(error.source, at)) != std::string::npos; at += file.size()) message.replace(at, error.source.size(), file);
        size_t cut = 0;
        for (int line = 0; line < 3 && cut != std::string::npos; ++line) cut = message.find('\n', cut == 0 ? 0 : cut + 1);
        ctx.toast(std::format("Shader error in {}\n{}", file, message.substr(0, cut)), ToastKind::Error);
    }
    m_engine.beginUi();
    ctx.hints.clear();
    ctx.modeLabel = "EDIT";
    ctx.panels.clear();
    ctx.rightColumnTop = ImGui::GetMainViewport()->WorkPos.y + (ctx.zen ? 12.0f : 122.0f) + (ctx.showStats && !ctx.zen ? 5.0f * (ImGui::GetTextLineHeight() + 2.0f) + 14.0f : 0.0f);

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

    m_outliner.draw(ctx);
    m_look.draw(ctx, m_lookState);
    m_look.drawWorld(ctx);
    m_inspector.draw(ctx);
    drawStats(m_frameMs);
    m_hud.drawTimeline(ctx);
    m_hud.drawStatusBar(ctx, m_commands, m_frameMs);
    drawQuickAdd();
    drawRecovery();
    drawUnsavedPrompt();
    m_hud.drawHelp(ctx, m_commands);
    m_palette.draw(ctx, m_commands);
    m_hud.drawToasts(ctx, deltaTime);

    if (m_stepOnce && ctx.playing && ctx.paused) {
        m_engine.setPaused(false);
        m_engine.tick(1.0f / 60.0f);
        m_engine.setPaused(true);
        m_stepOnce = false;
    } else {
        m_engine.tick(deltaTime);
    }
    if (m_lookState.animateStress) m_engine.setAnimation(static_cast<float>(now), m_movingObjectCount, 1);

    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || ImGui::IsAnyMouseDown() || io.InputQueueCharacters.Size > 0 || ImGui::IsKeyDown(ImGuiKey_Space) || io.KeyCtrl || io.KeyShift || io.KeyAlt) m_lastInputTime = now;
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && m_lastInputTime != now; ++key) {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key))) m_lastInputTime = now;
    }
    if (!ctx.playing && m_autosaveInterval > 0.0f) {
        m_autosaveTimer += deltaTime;
        if (m_autosaveTimer >= m_autosaveInterval && now - m_lastInputTime >= 3.0) {
            m_autosaveTimer = 0.0f;
            if (ctx.dirty() && ctx.world.saveScene(kAutosavePath)) ctx.toast("Autosaved");
        }
    }
    if (const size_t over = m_engine.renderOverCapacity(); over != m_reportedOverCapacity) {
        m_reportedOverCapacity = over;
        if (over > 0) ctx.toast(std::format("{} entities exceed GPU memory; the viewport is paused. Undo (Ctrl+Z) or delete entities to resume.", over), ToastKind::Error);
    }
    trackRenderSettings();
    updateTitle();
    m_engine.update();
    InputManager::Update();
    m_engine.present();
    if (m_framesPresented++ == 0) Lit::Log::Phase("first frame presented");
    glfwPollEvents();
}
