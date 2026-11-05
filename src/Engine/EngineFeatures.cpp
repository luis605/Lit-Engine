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

StreamingSystem& Engine::enableStreaming(const StreamingConfig& config) {
    m_streaming = std::make_unique<StreamingSystem>(m_portals, config);
    addSystem(Phase::PostUpdate, "streaming", [this](World& world, float) {
        int cell = m_portals.cameraCell();
        if (cell < 0) cell = m_portals.cellAt(world.camera().getPosition());
        m_streaming->update(cell);
    }, true);
    return *m_streaming;
}

DeltaStore& Engine::enableDeltas() {
    if (!m_deltas) m_deltas = std::make_unique<DeltaStore>(m_world, &m_portals);
    return *m_deltas;
}

StateStore& Engine::enableState(const std::string& schemaPath) {
    if (!m_state) m_state = std::make_unique<StateStore>(&m_world.events());
    if (!schemaPath.empty()) {
        std::string error;
        if (!m_state->loadSchemaFile(schemaPath, &error)) Lit::Log::Error("State schema '{}' failed: {}", schemaPath, error);
    }
    return *m_state;
}

StoryletEngine& Engine::enableStorylets(const std::string& storyletPath) {
    enableState();
    if (!m_storylets) {
        m_storylets = std::make_unique<StoryletEngine>(*m_state, m_world.random(), &m_world.events());
        addSystem(Phase::Update, "storylets", [this](World&, float dt) { m_storylets->update(dt); });
    }
    if (!storyletPath.empty()) {
        std::string error;
        if (!m_storylets->loadFile(storyletPath, &error)) Lit::Log::Error("Storylets '{}' failed: {}", storyletPath, error);
    }
    return *m_storylets;
}

ObservationSystem& Engine::enableObservation(const ObservationConfig& config, bool gpu) {
    m_observation = std::make_unique<ObservationSystem>(m_world, config);
    m_gpuObservation = gpu;
    addSystem(Phase::PostUpdate, "observation", [this](World& world, float dt) {
        const float aspect = m_windowHeight > 0 ? static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight) : 16.0f / 9.0f;
        world.syncCamera();
        m_observation->setView(observationViewFromCamera(world.camera(), aspect));
        m_observation->update(dt);
    });
    return *m_observation;
}

SaveSystem& Engine::enableSave(const std::string& autosaveDir, float autosaveSeconds, uint32_t slots) {
    SaveContext context;
    context.world = &m_world;
    context.state = m_state.get();
    context.storylets = m_storylets.get();
    context.params = &m_params;
    context.observation = m_observation.get();
    context.deltas = m_deltas.get();
    m_save = std::make_unique<SaveSystem>(context);
    if (!autosaveDir.empty()) {
        m_save->configureAutosave(autosaveDir, autosaveSeconds, slots);
        if (autosaveSeconds > 0.0f) addSystem(Phase::PostUpdate, "autosave", [this](World&, float dt) { m_save->autosaveAsync(dt); });
    }
    return *m_save;
}

void Engine::feedGpuObservation() {
    if (!m_observation || !m_gpuObservation) return;
    ObservationQuery query;
    for (EntityHandle e : m_observation->trackedEntities()) {
        if (!m_world.isAlive(e) || query.spheres.size() >= ObservationPass::kMaxSpheres) continue;
        const glm::vec4 bounds = m_world.getWorldBounds(e);
        if (bounds.w <= 0.0f) continue;
        query.spheres.push_back(bounds);
        query.ids.push_back(e.index + 1);
    }
    const Camera& camera = m_world.camera();
    query.invViewProjection = glm::inverse(camera.getProjectionMatrix() * camera.getViewMatrix());
    query.origin = camera.getPosition();
    query.frame = m_observation->frame();
    query.time = m_observation->time();
    m_renderer.setObservationQuery(std::move(query));
}

void Engine::submitGpuObservation() {
    if (!m_observation || !m_gpuObservation) return;
    ObservationReadback readback;
    while (m_renderer.takeObservation(readback)) m_observation->submitGpuIds(readback.frame, readback.time, readback.width, readback.height, readback.ids);
}

