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

constexpr uint32_t kParticleCount = 20000;
constexpr uint32_t kParticleGroupSize = 256;
constexpr int kMaterialRowCount = 7;
constexpr float kMaterialRowSpacing = 3.0f;
constexpr float kMaterialRowStart = -9.0f;
constexpr float kShowcaseTimeOfDay = 16.8f;
constexpr float kShowcaseSunAzimuth = 200.0f;
constexpr float kShowcaseFarPlane = 1500.0f;
constexpr uint32_t kParticleSeed = 3;

struct Particle {
    glm::vec4 position;
    glm::vec4 velocity;
};

class ShowcaseBuilder {
  public:
    ShowcaseBuilder(Engine& engine, World& world, uint32_t cubeMesh)
        : m_engine(engine), m_world(world), m_shaderNames(engine.shaderNames()), m_cube(cubeMesh),
          m_sphere(engine.loadMesh("sphere_hd")), m_torus(engine.loadMesh("torus")), m_plane(engine.loadMesh("plane")) {}

    template <typename Edit>
    uint32_t material(const std::string& name, Edit&& edit) {
        Material created;
        created.name = name;
        edit(created);
        return m_engine.createMaterial(created);
    }

    uint32_t shader(const std::string& name) const { return findNameIndex(m_shaderNames, name).value_or(0u); }

    EntityHandle spawn(const std::string& name, uint32_t mesh, uint32_t materialId, const glm::vec3& position, const glm::vec3& scale, uint32_t surface = 0, float alpha = 1.0f) {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = mesh;
        desc.material = materialId;
        desc.shader = surface;
        desc.alpha = alpha;
        desc.position = position;
        desc.scale = scale;
        return m_world.create(desc);
    }

    void light(const std::string& name, const glm::vec3& position, const std::string& text) {
        EntityDesc desc;
        desc.name = name;
        desc.mesh = NO_MESH;
        desc.position = position;
        const EntityHandle entity = m_world.create(desc);
        m_world.setComponentFromText(entity, "Light", text);
    }

    void buildGroundAndSpheres() {
        const uint32_t ground = material("Ground", [](Material& created) {
            created.baseColor = glm::vec3(0.42f, 0.42f, 0.40f);
            created.roughness = 0.75f;
            created.uvScale = 40.0f;
        });
        spawn("Ground", m_plane, ground, glm::vec3(0.0f), glm::vec3(60.0f, 1.0f, 60.0f), shader("Checker"));
        for (int row = 0; row < kMaterialRowCount; ++row) {
            const float roughness = static_cast<float>(row) / static_cast<float>(kMaterialRowCount - 1);
            const float x = kMaterialRowStart + static_cast<float>(row) * kMaterialRowSpacing;
            const uint32_t gold = material(std::format("Gold r{:.2f}", roughness), [roughness](Material& created) {
                created.baseColor = glm::vec3(1.0f, 0.766f, 0.336f);
                created.metallic = 1.0f;
                created.roughness = roughness;
            });
            const uint32_t plastic = material(std::format("Plastic r{:.2f}", roughness), [roughness](Material& created) {
                created.baseColor = glm::vec3(0.72f, 0.08f, 0.06f);
                created.metallic = 0.0f;
                created.roughness = roughness;
            });
            spawn(std::format("Gold {}", row), m_sphere, gold, glm::vec3(x, 1.0f, -3.0f), glm::vec3(1.0f));
            spawn(std::format("Plastic {}", row), m_sphere, plastic, glm::vec3(x, 1.0f, 0.5f), glm::vec3(1.0f));
        }
    }

    void buildMaterialShowcase() {
        const uint32_t carPaint = material("Car paint", [](Material& created) {
            created.baseColor = glm::vec3(0.05f, 0.18f, 0.55f);
            created.metallic = 0.6f;
            created.roughness = 0.45f;
            created.clearcoat = 1.0f;
            created.clearcoatRoughness = 0.03f;
        });
        const uint32_t velvet = material("Velvet", [](Material& created) {
            created.baseColor = glm::vec3(0.35f, 0.03f, 0.12f);
            created.roughness = 0.85f;
            created.sheenColor = glm::vec3(1.0f, 0.55f, 0.75f);
            created.sheenRoughness = 0.3f;
        });
        const uint32_t brushed = material("Brushed steel", [](Material& created) {
            created.baseColor = glm::vec3(0.92f, 0.92f, 0.93f);
            created.metallic = 1.0f;
            created.roughness = 0.35f;
            created.anisotropy = 0.85f;
        });
        const uint32_t wax = material("Wax", [](Material& created) {
            created.baseColor = glm::vec3(0.9f, 0.75f, 0.55f);
            created.roughness = 0.5f;
            created.subsurface = 0.8f;
            created.subsurfaceColor = glm::vec3(1.0f, 0.35f, 0.15f);
            created.thickness = 0.3f;
        });
        const uint32_t glass = material("Glass", [](Material& created) {
            created.baseColor = glm::vec3(0.8f, 0.95f, 1.0f);
            created.roughness = 0.03f;
            created.opacity = 0.3f;
            created.transmission = 1.0f;
        });
        const uint32_t clay = material("Clay", [](Material& created) {
            created.baseColor = glm::vec3(0.75f);
            created.roughness = 0.6f;
        });
        const uint32_t neon = material("Neon", [](Material& created) {
            created.baseColor = glm::vec3(0.1f);
            created.emissive = glm::vec3(0.2f, 0.8f, 1.0f);
            created.emissiveIntensity = 40000.0f;
        });
        spawn("Car paint torus", m_torus, carPaint, glm::vec3(-9.0f, 0.4f, 5.0f), glm::vec3(1.2f));
        spawn("Velvet", m_sphere, velvet, glm::vec3(-6.0f, 1.0f, 5.0f), glm::vec3(1.0f));
        const EntityHandle brushedTorus = spawn("Brushed torus", m_torus, brushed, glm::vec3(-3.0f, 1.3f, 5.0f), glm::vec3(1.2f));
        m_world.setRotation(brushedTorus, glm::angleAxis(glm::radians(70.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
        spawn("Wax", m_sphere, wax, glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(1.0f));
        spawn("Lava", m_sphere, clay, glm::vec3(3.0f, 1.2f, 5.0f), glm::vec3(1.0f), shader("Lava"));
        spawn("Marble", m_sphere, clay, glm::vec3(6.0f, 1.0f, 5.0f), glm::vec3(1.0f), shader("Marble"));
        spawn("Glass", m_sphere, glass, glm::vec3(9.0f, 1.0f, 5.0f), glm::vec3(1.0f), shader("Glass"));
        spawn("Tinted", m_sphere, clay, glm::vec3(10.5f, 1.0f, 8.0f), glm::vec3(1.0f), 0, 0.5f);
        spawn("Neon bar", m_cube, neon, glm::vec3(0.0f, 0.25f, 9.0f), glm::vec3(8.0f, 0.12f, 0.12f));
        spawn("Monolith", m_cube, clay, glm::vec3(13.0f, 3.0f, 0.0f), glm::vec3(0.6f, 3.0f, 2.0f));
        spawn("Wall", m_cube, clay, glm::vec3(0.0f, 2.0f, -8.0f), glm::vec3(14.0f, 2.0f, 0.3f));
    }

    void buildLights() {
        light("Warm lamp", glm::vec3(-7.0f, 2.5f, 2.5f), "1 1 0.62 0.3 8000 18 1 20 30");
        light("Cyan lamp", glm::vec3(7.0f, 2.0f, 2.5f), "1 0.3 0.8 1 6000 16 1 20 30");
    }

    void buildParticles() {
        std::vector<Particle> particles(kParticleCount);
        std::mt19937 rng(kParticleSeed);
        std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
        for (Particle& particle : particles) {
            const glm::vec3 offset(spread(rng) * 4.0f, spread(rng) * 1.5f, spread(rng) * 4.0f);
            particle.position = glm::vec4(glm::vec3(0.0f, 3.5f, 2.0f) + offset, std::abs(spread(rng)));
            particle.velocity = glm::vec4(glm::cross(glm::normalize(offset + glm::vec3(1e-3f)), glm::vec3(0.0f, 1.0f, 0.0f)) * 2.5f, 0.0f);
        }
        m_engine.setShaderBuffer("Particles", particles);
        const uint32_t simulate = m_engine.addCompute({"Particle simulation", "resources/shaders/compute/particles.comp", {(kParticleCount + kParticleGroupSize - 1) / kParticleGroupSize, 1, 1}, ComputeStage::BeforeScene});
        m_engine.setPassParam(PassKind::Compute, simulate, 0, glm::vec4(0.0f, 3.5f, 2.0f, static_cast<float>(kParticleCount)));
        DrawDesc sparks;
        sparks.name = "Particles";
        sparks.vertexPath = "resources/shaders/draws/particles.vert";
        sparks.fragmentPath = "resources/shaders/draws/particles.frag";
        sparks.primitive = DrawPrimitive::Points;
        sparks.vertexCount = kParticleCount;
        sparks.blend = DrawBlend::Additive;
        sparks.depthWrite = false;
        m_engine.addDraw(sparks);
        if (!m_engine.meshShadersSupported()) return;
        DrawDesc wave;
        wave.name = "Mesh shader wave";
        wave.meshPath = "resources/shaders/draws/wave.mesh";
        wave.fragmentPath = "resources/shaders/draws/wave.frag";
        wave.meshGroups = glm::ivec3(32, 1, 1);
        const uint32_t waveId = m_engine.addDraw(wave);
        m_engine.setPassParam(PassKind::Draw, waveId, 0, glm::vec4(-22.0f, 0.2f, -2.0f, 0.0f));
    }

  private:
    Engine& m_engine;
    World& m_world;
    std::vector<std::string> m_shaderNames;
    uint32_t m_cube;
    uint32_t m_sphere;
    uint32_t m_torus;
    uint32_t m_plane;
};

}

void Application::buildShowcaseScene() {
    World& world = m_ctx->world;
    ShowcaseBuilder builder(m_engine, world, m_ctx->cubeMesh);
    builder.buildGroundAndSpheres();
    builder.buildMaterialShowcase();
    builder.buildLights();
    builder.buildParticles();
    m_movingObjectCount = 0;
    RenderSettings& settings = m_engine.renderSettings();
    settings.timeOfDay = kShowcaseTimeOfDay;
    settings.sunAzimuth = kShowcaseSunAzimuth;
    Camera& camera = world.camera();
    camera.setPos(glm::vec3(0.0f, 4.5f, 17.0f));
    camera.setOrientation(-90.0f, -14.0f);
    camera.setFarPlane(kShowcaseFarPlane);
}
