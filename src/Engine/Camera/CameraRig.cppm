module;

#include <functional>
#include <optional>

export module Engine.CameraRig;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;

export enum class CameraMode { FirstPerson, ThirdPerson };

export struct CameraRigSettings {
    float eyeHeight = 1.6f;
    float thirdDistance = 3.5f;
    float shoulderOffset = 0.0f;
    float collisionRadius = 0.25f;
    float minDistance = 0.2f;
    float maxPitch = 85.0f;
};

export using CameraCollisionQuery = std::function<std::optional<float>(const glm::vec3& origin, const glm::vec3& direction, float maxDistance)>;

export class CameraRig {
  public:
    explicit CameraRig(World& world, CameraRigSettings settings = {}) : m_world(world), m_settings(settings) {
        m_pivot = world.camera().getPosition();
    }

    void setTarget(EntityHandle target) { m_target = target; }
    [[nodiscard]] EntityHandle target() const { return m_target; }
    void setMode(CameraMode mode);
    CameraMode toggle();
    [[nodiscard]] CameraMode mode() const { return m_mode; }
    void setSettings(const CameraRigSettings& s) { m_settings = s; }
    [[nodiscard]] const CameraRigSettings& settings() const { return m_settings; }
    void setCollisionQuery(CameraCollisionQuery query) { m_query = std::move(query); }
    void setPivot(const glm::vec3& pivot) { m_pivot = pivot; }
    [[nodiscard]] glm::vec3 pivot() const;
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] float currentDistance() const { return m_distance; }

    void look(float yawDeltaDegrees, float pitchDeltaDegrees);
    void update();

  private:
    [[nodiscard]] float collide(const glm::vec3& pivot, const glm::vec3& direction, float wanted);

    World& m_world;
    CameraRigSettings m_settings;
    CameraCollisionQuery m_query;
    EntityHandle m_target;
    CameraMode m_mode = CameraMode::FirstPerson;
    glm::vec3 m_pivot{0.0f};
    float m_distance = 0.0f;
};
