module;

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

export module Engine.Observation;

import Engine.glm;
import Engine.Json;
import Engine.camera;
import Engine.Render.entity;
import Engine.World;

export enum class ObservationState : uint8_t { Unobserved, Seen, Attended };

export struct ObservationChanged {
    EntityHandle entity;
    ObservationState from = ObservationState::Unobserved;
    ObservationState to = ObservationState::Unobserved;
    double time = 0.0;
};

export struct ObservationView {
    glm::vec3 origin{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovY = 45.0f;
    float aspect = 16.0f / 9.0f;
};

export [[nodiscard]] ObservationView observationViewFromCamera(const Camera& camera, float aspect);

export struct ObservationConfig {
    float focusConeDegrees = 6.0f;
    float focusExitScale = 1.5f;
    float maxDistance = 200.0f;
    float seenEnter = 0.05f;
    float seenExit = 0.5f;
    float attendEnter = 0.25f;
    float attendExit = 0.4f;
    float maxLatencySeconds = 0.1f;
    uint32_t maxLatencyFrames = 4;
    float logIntervalSeconds = 10.0f;
    bool logLatency = true;
};

export struct ObservationStats {
    uint64_t gpuSamples = 0;
    uint64_t gpuRejected = 0;
    uint64_t cpuUpdates = 0;
    double lastLatencySeconds = 0.0;
    double maxLatencySeconds = 0.0;
    double meanLatencySeconds = 0.0;
    uint32_t lastLatencyFrames = 0;
    uint32_t maxLatencyFrames = 0;
    bool usingGpu = false;
};

export class ObservationSystem {
  public:
    explicit ObservationSystem(World& world, ObservationConfig config = {}) : m_world(world), m_config(config) {}

    void track(EntityHandle e);
    void untrack(EntityHandle e);
    [[nodiscard]] std::vector<EntityHandle> trackedEntities() const {
        std::vector<EntityHandle> out;
        out.reserve(m_entities.size());
        for (const auto& [index, t] : m_entities) out.push_back(t.handle);
        return out;
    }
    [[nodiscard]] bool tracked(EntityHandle e) const { return m_entities.contains(e.index) && m_entities.at(e.index).handle == e; }
    [[nodiscard]] ObservationState state(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> inState(ObservationState s) const;
    [[nodiscard]] std::vector<EntityHandle> attended() const { return inState(ObservationState::Attended); }

    void setIgnore(EntityHandle e) { m_ignore = e; }
    void setView(const ObservationView& view) { m_view = view; }
    [[nodiscard]] const ObservationView& view() const { return m_view; }
    void setConfig(const ObservationConfig& config) { m_config = config; }
    [[nodiscard]] const ObservationConfig& config() const { return m_config; }

    [[nodiscard]] double time() const { return m_time; }
    [[nodiscard]] uint64_t frame() const { return m_frame; }
    void submitGpuIds(uint64_t frameIssued, double issuedTime, uint32_t width, uint32_t height, std::span<const uint32_t> ids);
    void update(float dt);

    [[nodiscard]] const ObservationStats& stats() const { return m_stats; }
    [[nodiscard]] EntityHandle focusEntity() const { return m_focus; }
    [[nodiscard]] Json toJson() const;
    bool fromJson(const Json& doc, const std::function<EntityHandle(EntityHandle)>& remap = {});

  private:
    struct Tracked {
        EntityHandle handle;
        ObservationState state = ObservationState::Unobserved;
        ObservationState candidate = ObservationState::Unobserved;
        double candidateSince = 0.0;
    };
    struct Sample {
        uint64_t frame = 0;
        double time = 0.0;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint32_t> ids;
    };

    ObservationState rawCpu(const Tracked& t, float coneDegrees, float& angleOut);
    void rawGpu(std::unordered_map<uint32_t, ObservationState>& out);
    void apply(Tracked& t, ObservationState raw);

    World& m_world;
    ObservationConfig m_config;
    ObservationView m_view;
    ObservationStats m_stats;
    std::unordered_map<uint32_t, Tracked> m_entities;
    Sample m_sample;
    bool m_haveSample = false;
    EntityHandle m_focus;
    EntityHandle m_ignore;
    double m_time = 0.0;
    uint64_t m_frame = 0;
    double m_latencySum = 0.0;
    double m_nextLog = 0.0;
};
