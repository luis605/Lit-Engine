module;

#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

export module Engine.AudioTrace;

import Engine.glm;
import Engine.Geometry;

export struct AudioHop {
    glm::vec3 entry{0.0f};
    glm::vec3 exit{0.0f};
    float cost = 0.0f;
    int id = -1;
};

export using AudioHopFn = std::function<void(const glm::vec3& from, std::vector<AudioHop>& out)>;

export struct AudioReflection {
    float delay = 0.0f;
    float gain = 0.0f;
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
};

export struct ReverbEstimate {
    float rt60 = 0.0f;
    float wet = 0.0f;
    float meanFreePath = 0.0f;
    float escape = 1.0f;
};

export struct AudioPath {
    bool audible = false;
    bool direct = false;
    bool viaHop = false;
    bool stale = true;
    int hopId = -1;
    int hops = 0;
    float directOcclusion = 0.0f;
    float occlusion = 0.0f;
    float pathLength = 0.0f;
    float attenuation = 0.0f;
    float lowpassHz = 20000.0f;
    glm::vec3 arrivalDirection{0.0f, 0.0f, 1.0f};
    std::vector<AudioReflection> reflections;
    float reverbSend = 0.0f;
    uint32_t raysUsed = 0;
};

export struct AudioPropagationConfig {
    uint32_t raysPerFrame = 256;
    int occlusionRays = 8;
    int reflectionCandidates = 6;
    int reverbRays = 32;
    float maxDistance = 60.0f;
    float referenceDistance = 1.0f;
    float speedOfSound = 343.0f;
    float wallAbsorption = 0.35f;
    float occludedTransmission = 0.12f;
    float sourceRadius = 0.3f;
    float hopOcclusionThreshold = 0.5f;
    int maxHops = 2;
    float occludedCutoffHz = 700.0f;
};

export struct AudioEmitter {
    uint32_t id = 0;
    glm::vec3 position{0.0f};
};

export class AudioPropagation {
  public:
    void setScene(const TriangleSoup& soup);
    void setHopHook(AudioHopFn hook) { m_hook = std::move(hook); }
    [[nodiscard]] AudioPropagationConfig& config() { return m_config; }
    [[nodiscard]] const AudioPropagationConfig& config() const { return m_config; }

    void beginFrame(const glm::vec3& listener);
    AudioPath trace(const glm::vec3& listener, const glm::vec3& source);
    void update(const glm::vec3& listener, const std::vector<AudioEmitter>& emitters);
    [[nodiscard]] const AudioPath* result(uint32_t id) const;
    [[nodiscard]] ReverbEstimate reverb() const { return m_reverb; }
    [[nodiscard]] uint32_t raysUsed() const { return m_used; }
    [[nodiscard]] uint32_t raysRemaining() const { return m_used >= m_config.raysPerFrame ? 0 : m_config.raysPerFrame - m_used; }
    [[nodiscard]] uint32_t staleCount() const { return m_stale; }
    [[nodiscard]] const Bvh& bvh() const { return m_bvh; }

  private:
    struct FanHit {
        glm::vec3 direction{0.0f};
        glm::vec3 point{0.0f};
        float distance = 0.0f;
        bool hit = false;
    };
    struct ResultSlot {
        AudioPath path;
        uint64_t frame = 0;
    };
    void traceInto(AudioPath& path, const glm::vec3& listener, const glm::vec3& source);
    bool blocked(const glm::vec3& from, const glm::vec3& to);
    bool canCast() const { return m_used < m_config.raysPerFrame; }
    bool hopSearch(const glm::vec3& from, const glm::vec3& source, int depth, float& length, glm::vec3& firstEntry, int& hopId, int& hops);
    [[nodiscard]] float distanceGain(float distance) const;
    float measureOcclusion(const glm::vec3& listener, const glm::vec3& source, const glm::vec3& direction);
    void applyHopRoute(AudioPath& path, const glm::vec3& listener, const glm::vec3& source);
    void collectReflections(AudioPath& path, const glm::vec3& listener, const glm::vec3& source, float directDistance);
    void markStale(const AudioEmitter& emitter, const glm::vec3& listener, float distance);

    AudioPropagationConfig m_config;
    Bvh m_bvh;
    AudioHopFn m_hook;
    glm::vec3 m_listener{0.0f};
    std::vector<FanHit> m_fan;
    ReverbEstimate m_reverb;
    uint32_t m_used = 0;
    uint32_t m_stale = 0;
    std::map<uint32_t, ResultSlot> m_results;
    uint64_t m_frame = 0;
    std::vector<std::pair<float, uint32_t>> m_order;
    std::vector<std::pair<float, uint32_t>> m_reflectionOrder;
    std::vector<std::vector<AudioHop>> m_hopCandidates;
};
