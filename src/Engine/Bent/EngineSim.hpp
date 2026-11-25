#pragma once

// Internal to BentEngine. Include after the engine module imports (Engine.engine, Engine.World, Engine.Physics, Engine.Nav, ...).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Engine/Bent/EngineHost.hpp"

namespace bent {

inline constexpr float kBentStep = 1.0f / 60.0f;

struct StreamGrid {
    bool enabled = false;
    int first = 0;
    int nx = 0;
    int nz = 0;
    float cellSize = 100.0f;
    float loadRadius = 150.0f;
    float keepRadius = 200.0f;
    int perCell = 3;
};

struct NavState {
    std::unique_ptr<NavMesh> mesh;
    std::vector<Aabb> obstacles;
    float half = 20.0f;
    float radius = 0.4f;
    float cell = 0.5f;
};

class EngineSim final : public Sim {
  public:
    EngineSim();
    ~EngineSim() override;

    void apply(const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick) override;
    void tick() override;
    Value observe(const std::string& name, Interp& interp, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) override;
    [[nodiscard]] uint64_t ticks() const override { return m_ticks; }

    // Replay support (replayMatches): record input to `path`, or replay it and ignore input actions.
    void startRecording(const std::string& path);
    void stopRecording();
    bool startReplay(const std::string& path);
    [[nodiscard]] uint64_t replayHash();

    [[nodiscard]] Engine& engine() { return *m_engine; }
    [[nodiscard]] World& world() { return m_engine->world(); }
    [[nodiscard]] EntityHandle resolve(const Value& target, const SrcLoc& loc, bool allowNull = false) const;
    [[nodiscard]] Value refOf(EntityHandle handle);

    std::shared_ptr<void> extWorld;  // per-sim state of Ext_World.cpp (declared first: destroyed after the engine)
    std::unique_ptr<Engine> m_engine;
    std::vector<EntityHandle> m_refs;
    std::map<EntityHandle, int64_t, bool (*)(const EntityHandle&, const EntityHandle&)> m_refIndex{[](const EntityHandle& a, const EntityHandle& b) { return a.index != b.index ? a.index < b.index : a.generation < b.generation; }};
    std::map<int64_t, WorldSnapshot> m_snapshots;
    std::map<int64_t, std::string> m_textSnapshots;
    StreamGrid m_grid;
    NavState m_nav;
    bool m_terrain = false;
    bool m_replaying = false;
    bool m_flyCamera = false;
    bool m_deltas = false;
    InputState m_input;
    uint64_t m_ticks = 0;
    // Portal crossings of the camera (cameraWalk): largest pose discontinuity against the portal transform, count, ping-pongs.
    double m_portalError = 0.0;
    int64_t m_crossings = 0;
    int64_t m_pingPongs = 0;
    int m_lastCrossed = -1;
    int64_t m_impulses = 0;
    std::vector<glm::vec3> m_pendingForces;  // per ref, applied for m_forceTicks
    std::vector<int64_t> m_forceTicks;

  private:
    void addRef(EntityHandle handle);
    void remapRefs(const std::vector<EntityHandle>& before, const std::vector<EntityHandle>& after);
    void stepTicks(int64_t count, const std::function<bool()>& onTick);
    void doTick();
    void walkCamera(const glm::vec3& delta, int64_t steps, const std::function<bool()>& onTick);
};

// Helpers shared by the action and observation files.
[[nodiscard]] glm::vec3 toVec(const Value& value, const SrcLoc& loc, const char* what);
[[nodiscard]] glm::quat toQuat(const Value& value, const SrcLoc& loc, const char* what);
[[nodiscard]] Value fromVec(const glm::vec3& v);
[[nodiscard]] Value fromDVec(const glm::dvec3& v);
[[nodiscard]] Value fromQuat(const glm::quat& q);
[[nodiscard]] uint64_t contentHash(World& world, bool absolute);
[[nodiscard]] uint64_t relativeHash(World& world);
[[nodiscard]] Value jsonToValue(const Json& json);
[[nodiscard]] Json valueToJson(const Value& value, const SrcLoc& loc);
[[nodiscard]] const std::vector<ActionSpec>& engineActionSpecs();
[[nodiscard]] const std::vector<ObservationSpec>& engineObservationSpecs();

}
