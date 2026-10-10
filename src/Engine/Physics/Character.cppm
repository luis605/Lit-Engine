module;

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

export module Engine.Character;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.CameraRig;

export struct CharacterSettings {
    float radius = 0.35f;
    float height = 1.8f;
    float stepHeight = 0.4f;
    float slopeLimit = 50.0f;
    float speed = 4.0f;
    float sprintScale = 1.8f;
    float acceleration = 40.0f;
    float airAcceleration = 8.0f;
    float jumpSpeed = 4.5f;
    float upResponse = 8.0f;
    float impactMinSpeed = 1.5f;
    float impactMaxShove = 3.0f;
    float impactKick = 1.5f;
    float impactKickMax = 8.0f;
    float kickDecay = 8.0f;
    float mass = 70.0f;
    float substep = 1.0f / 120.0f;
};

export struct CharacterInput {
    glm::vec2 move{0.0f};
    bool jump = false;
    bool sprint = false;
};

export struct CharacterImpact {
    EntityHandle source;
    glm::vec3 direction{0.0f};
    float speed = 0.0f;
    glm::vec3 impulse{0.0f};
};

export class CharacterController {
  public:
    CharacterController(World& world, PortalSystem& portals, const PhysicsSettings& physics, CharacterSettings settings = {}, CameraRigSettings rig = {});
    ~CharacterController();
    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;

    void spawn(const glm::vec3& feet, float yawDegrees, float pitchDegrees = 0.0f);
    void setInput(const CharacterInput& input) { m_input = input; }
    void look(float yawDeltaDegrees, float pitchDeltaDegrees);
    void update(float dt);
    void applyImpulse(const glm::vec3& deltaVelocity) { m_velocity += deltaVelocity; }
    void setImpactHook(std::function<void(const CharacterImpact&)> hook) { m_impactHook = std::move(hook); }

    [[nodiscard]] CameraRig& rig() { return m_rig; }
    [[nodiscard]] EntityHandle entity() const { return m_entity; }
    [[nodiscard]] const glm::vec3& position() const { return m_position; }
    [[nodiscard]] const glm::vec3& velocity() const { return m_velocity; }
    [[nodiscard]] const glm::vec3& up() const { return m_up; }
    [[nodiscard]] const glm::vec3& groundNormal() const { return m_groundNormal; }
    [[nodiscard]] bool grounded() const { return m_grounded; }
    [[nodiscard]] int cell() const { return m_cell; }
    [[nodiscard]] int crossings() const { return m_crossings; }
    [[nodiscard]] float yaw() const { return m_yaw; }
    [[nodiscard]] float pitch() const { return m_pitch; }
    [[nodiscard]] glm::vec2 kick() const { return m_kick; }
    [[nodiscard]] glm::vec3 gravity() const;
    [[nodiscard]] glm::vec3 eye() const;

  private:
    [[nodiscard]] std::vector<PhysicsContact> gather(const glm::vec3& position);
    [[nodiscard]] bool filtered(EntityHandle e) const;
    void step(float h);
    void move(const glm::vec3& delta);
    bool resolve(glm::vec3& position, bool* wall);
    bool tryStep(const glm::vec3& start, const glm::vec3& delta);
    void snapDown();
    void checkCross(const glm::vec3& previousProbe);
    [[nodiscard]] glm::vec3 probe() const;
    void updateCamera();
    void consumeImpacts();

    World& m_world;
    PortalSystem& m_portals;
    const PhysicsSettings& m_physics;
    CharacterSettings m_s;
    CameraRig m_rig;
    CharacterInput m_input;
    EntityHandle m_entity;
    EntityHandle m_collider;
    glm::vec3 m_position{0.0f};
    glm::vec3 m_velocity{0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_groundNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 m_probe{0.0f};
    glm::vec2 m_kick{0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    bool m_grounded = false;
    bool m_swept = false;
    PortalCast m_lastCast;
    int m_cell = -1;
    int m_crossings = 0;
    uint32_t m_subscription = 0;
    std::vector<CharacterImpact> m_pending;
    std::function<void(const CharacterImpact&)> m_impactHook;
};
