module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

module Engine.Character;

import Engine.camera;

namespace {
constexpr float kMinContactDepth = 1.0e-4f;
constexpr int kMaxResolveIterations = 8;
constexpr float kWallNormalLimit = 0.3f;
constexpr float kExitNormalLimit = 0.7f;
constexpr float kStepProbeMargin = 0.02f;
constexpr float kMinGravity = 1.0e-4f;
constexpr float kMinDirection = 1.0e-5f;
constexpr float kMinDifference = 1.0e-6f;
constexpr float kMaxCameraPitch = 89.0f;
constexpr int kMaxSubsteps = 16;
constexpr float kMinSubstep = 1.0e-4f;

bool finiteVec(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

glm::vec3 rotate(const glm::mat4& matrix, const glm::vec3& vector) { return glm::vec3(matrix * glm::vec4(vector, 0.0f)); }
glm::vec3 place(const glm::mat4& matrix, const glm::vec3& point) { return glm::vec3(matrix * glm::vec4(point, 1.0f)); }

glm::vec3 flatForward(float yawDegrees) {
    const float yaw = glm::radians(yawDegrees);
    return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
}

float yawOf(const glm::vec3& forward) { return glm::degrees(std::atan2(forward.z, forward.x)); }
}

CharacterController::CharacterController(World& world, PortalSystem& portals, const PhysicsSettings& physics, CharacterSettings settings, CameraRigSettings rig)
    : m_world(world), m_portals(portals), m_physics(physics), m_settings(settings), m_rig(world, rig) {
    EntityDesc desc;
    desc.name = "Character";
    m_entity = world.create(desc);
    desc.name = "Character Collider";
    desc.parent = m_entity;
    desc.position = glm::vec3(0.0f, m_settings.height * 0.5f, 0.0f);
    m_collider = world.create(desc);
    world.add<BoxCollider>(m_collider, glm::vec3(m_settings.radius, m_settings.height * 0.5f, m_settings.radius));
    m_rig.setTarget(m_entity);
    m_rig.setCollisionQuery([this](const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> std::optional<float> {
        const PhysicsFilter skip = [this](EntityHandle entity) { return filtered(entity); };
        m_lastCast = portalSweep(m_portals, m_world, origin, direction, maxDistance, m_rig.settings().collisionRadius, m_cell, skip, true);
        m_swept = true;
        if (!m_lastCast.hit) return std::nullopt;
        return m_lastCast.distance;
    });
    m_subscription = world.events().subscribe<Collision>([this](const Collision& collision) {
        const bool first = collision.a == m_collider;
        if (!first && !(collision.b == m_collider)) return;
        if (collision.closingSpeed < m_settings.impactMinSpeed) return;
        const EntityHandle other = first ? collision.b : collision.a;
        const RigidBody* body = m_world.get<RigidBody>(other);
        if (!body || body->isStatic) return;
        CharacterImpact impact;
        impact.source = other;
        impact.direction = first ? -collision.normal : collision.normal;
        impact.speed = collision.closingSpeed;
        impact.impulse = impact.direction * (collision.closingSpeed * body->mass / m_settings.mass);
        m_pending.push_back(impact);
    });
}

CharacterController::~CharacterController() {
    m_world.events().unsubscribe(m_subscription);
    if (m_world.isAlive(m_entity)) m_world.destroy(m_entity);
}

void CharacterController::spawn(const glm::vec3& feet, float yawDegrees, float pitchDegrees) {
    if (!finiteVec(feet) || !std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) return;
    m_position = feet;
    m_velocity = glm::vec3(0.0f);
    m_yaw = yawDegrees;
    m_pitch = pitchDegrees;
    m_cell = std::max(m_portals.cellAt(feet + m_up * (m_settings.height * 0.5f)), 0);
    m_world.setPosition(m_entity, m_position);
    m_probe = probe();
    m_portals.setCameraCell(m_cell);
    updateCamera();
}

void CharacterController::look(float yawDeltaDegrees, float pitchDeltaDegrees) {
    if (!std::isfinite(yawDeltaDegrees) || !std::isfinite(pitchDeltaDegrees)) return;
    m_yaw += yawDeltaDegrees;
    m_pitch = std::clamp(m_pitch + pitchDeltaDegrees, -m_rig.settings().maxPitch, m_rig.settings().maxPitch);
}

glm::vec3 CharacterController::gravity() const {
    if (!m_portals.hasCells()) return m_physics.gravity;
    return m_portals.sampleParams(m_cell, m_position + m_up * (m_settings.height * 0.5f)).gravity;
}

glm::vec3 CharacterController::eye() const { return m_position + glm::vec3(0.0f, m_rig.settings().eyeHeight, 0.0f); }

glm::vec3 CharacterController::probe() const {
    if (m_rig.mode() != CameraMode::FirstPerson) return eye();
    const float yaw = glm::radians(m_yaw);
    const float pitch = glm::radians(m_pitch);
    const glm::vec3 forward(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
    return eye() + forward * m_world.camera().getNearPlane();
}

bool CharacterController::filtered(EntityHandle entity) const {
    if (entity == m_collider || entity == m_entity || m_portals.isGhost(entity)) return true;
    const RigidBody* body = const_cast<World&>(m_world).get<RigidBody>(entity);
    return body && !body->isStatic;
}

std::vector<PhysicsContact> CharacterController::gather(const glm::vec3& position) {
    const float radius = m_settings.radius;
    const glm::vec3 bottom = position + m_up * radius;
    const glm::vec3 top = position + m_up * (m_settings.height - radius);
    const PhysicsFilter skip = [this](EntityHandle entity) { return filtered(entity); };
    std::vector<PhysicsContact> contacts;
    for (PhysicsContact& contact : physicsOverlapCapsule(m_world, bottom, top, radius, skip)) {
        if (contact.depth > kMinContactDepth) contacts.push_back(contact);
    }
    if (!m_portals.active()) return contacts;
    const glm::vec3 center = (bottom + top) * 0.5f;
    const float reach = m_settings.height * 0.5f + radius;
    for (int portalId : m_portals.portalsOf(m_cell)) {
        const Portal& portal = m_portals.portals()[static_cast<size_t>(portalId)];
        const glm::vec3 offset = center - portal.center();
        const float side = glm::dot(offset, portal.normal());
        if (side < -radius || side > reach) continue;
        if (std::abs(glm::dot(offset, portal.right())) > portal.half.x + radius || std::abs(glm::dot(offset, portal.up())) > portal.half.y + reach) continue;
        const Portal& exit = m_portals.portals()[static_cast<size_t>(portal.partner)];
        const glm::mat3 back = glm::transpose(glm::mat3(portal.transform));
        for (const PhysicsContact& contact : physicsOverlapCapsule(m_world, place(portal.transform, bottom), place(portal.transform, top), radius, skip)) {
            if (contact.depth <= kMinContactDepth || glm::dot(contact.normal, exit.normal()) > kExitNormalLimit) continue;
            contacts.push_back({contact.entity, back * contact.normal, contact.depth});
        }
    }
    return contacts;
}

bool CharacterController::resolve(glm::vec3& position, bool* wall) {
    const float cosSlope = std::cos(glm::radians(m_settings.slopeLimit));
    bool grounded = false;
    for (int iteration = 0; iteration < kMaxResolveIterations; ++iteration) {
        const std::vector<PhysicsContact> contacts = gather(position);
        if (contacts.empty()) break;
        const PhysicsContact* deepest = &contacts[0];
        for (const PhysicsContact& contact : contacts) {
            if (contact.depth > deepest->depth) deepest = &contact;
        }
        const glm::vec3 normal = deepest->normal;
        position += normal * deepest->depth;
        const float intoSurface = glm::dot(m_velocity, normal);
        if (intoSurface < 0.0f) m_velocity -= normal * intoSurface;
        const float alignment = glm::dot(normal, m_up);
        if (alignment >= cosSlope) {
            grounded = true;
            m_groundNormal = normal;
        } else if (wall && std::abs(alignment) < kWallNormalLimit) {
            *wall = true;
        }
    }
    return grounded;
}

bool CharacterController::tryStep(const glm::vec3& start, const glm::vec3& delta) {
    const glm::vec3 horizontal = delta - m_up * glm::dot(delta, m_up);
    const glm::vec3 lifted = start + m_up * m_settings.stepHeight;
    if (!gather(lifted).empty()) return false;
    const glm::vec3 target = lifted + horizontal;
    if (!gather(target).empty()) return false;
    const PhysicsFilter skip = [this](EntityHandle entity) { return filtered(entity); };
    const glm::vec3 base = target + m_up * m_settings.radius;
    const auto hit = physicsSweepSphere(m_world, base, -m_up, m_settings.stepHeight + kStepProbeMargin, m_settings.radius, skip);
    if (!hit) return false;
    const glm::vec3 landed = target - m_up * hit->distance;
    if (glm::dot(hit->normal, m_up) < std::cos(glm::radians(m_settings.slopeLimit))) return false;
    if (!gather(landed).empty()) return false;
    m_position = landed;
    m_grounded = true;
    m_groundNormal = hit->normal;
    return true;
}

void CharacterController::snapDown() {
    const PhysicsFilter skip = [this](EntityHandle entity) { return filtered(entity); };
    const auto hit = physicsSweepSphere(m_world, m_position + m_up * m_settings.radius, -m_up, m_settings.stepHeight, m_settings.radius, skip);
    if (!hit || glm::dot(hit->normal, m_up) < std::cos(glm::radians(m_settings.slopeLimit))) return;
    m_position -= m_up * hit->distance;
    m_grounded = true;
    m_groundNormal = hit->normal;
    const float downSpeed = glm::dot(m_velocity, m_up);
    if (downSpeed < 0.0f) m_velocity -= m_up * downSpeed;
}

void CharacterController::move(const glm::vec3& delta) {
    const bool wasGrounded = m_grounded;
    const glm::vec3 start = m_position;
    m_position += delta;
    bool hitWall = false;
    m_grounded = resolve(m_position, &hitWall);
    if (wasGrounded && hitWall && !m_grounded) {
        const glm::vec3 keepPosition = m_position;
        const glm::vec3 keepVelocity = m_velocity;
        if (!tryStep(start, delta)) {
            m_position = keepPosition;
            m_velocity = keepVelocity;
        }
    }
    if (wasGrounded && !m_grounded && glm::dot(m_velocity, m_up) <= 0.0f) snapDown();
}

void CharacterController::checkCross(const glm::vec3& previousProbe) {
    if (!m_portals.active()) return;
    glm::vec3 moved = probe();
    int cell = m_cell;
    const int portalId = m_portals.stepPoint(moved, cell, previousProbe, true);
    if (portalId < 0) {
        const int resolved = m_portals.resolveCell(m_cell, probe());
        if (resolved != m_cell) {
            m_cell = resolved;
            m_portals.setCameraCell(resolved);
        }
        return;
    }
    const glm::mat4& transform = m_portals.portals()[static_cast<size_t>(portalId)].transform;
    m_position = place(transform, m_position);
    m_velocity = rotate(transform, m_velocity);
    m_up = glm::normalize(rotate(transform, m_up));
    m_yaw = yawOf(rotate(transform, flatForward(m_yaw)));
    m_cell = cell;
    ++m_crossings;
    m_portals.setCameraCell(cell);
}

void CharacterController::alignUp(const glm::vec3& gravityVector, float stepSeconds) {
    if (glm::length(gravityVector) <= kMinGravity) return;
    const glm::vec3 target = -glm::normalize(gravityVector);
    const glm::vec3 blended = glm::mix(m_up, target, 1.0f - std::exp(-m_settings.upResponse * stepSeconds));
    if (glm::length(blended) > kMinDirection) m_up = glm::normalize(blended);
}

glm::vec3 CharacterController::wishVelocity() const {
    glm::vec3 forward = flatForward(m_yaw);
    forward = forward - m_up * glm::dot(forward, m_up);
    forward = glm::length(forward) > kMinDirection ? glm::normalize(forward) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, m_up));
    const bool finiteMove = std::isfinite(m_input.move.x) && std::isfinite(m_input.move.y);
    const glm::vec2 move = finiteMove ? m_input.move : glm::vec2(0.0f);
    glm::vec3 wish = forward * move.y + right * move.x;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    wish *= m_settings.speed * (m_input.sprint ? m_settings.sprintScale : 1.0f);
    if (m_grounded) wish -= m_groundNormal * glm::dot(wish, m_groundNormal);
    return wish;
}

void CharacterController::accelerate(const glm::vec3& gravityVector, float stepSeconds) {
    const float vertical = glm::dot(m_velocity, m_up);
    const glm::vec3 horizontal = m_velocity - m_up * vertical;
    const glm::vec3 wish = wishVelocity();
    const glm::vec3 base = m_grounded ? m_velocity : horizontal;
    const glm::vec3 difference = wish - base;
    const float maxChange = (m_grounded ? m_settings.acceleration : m_settings.airAcceleration) * stepSeconds;
    const float differenceLength = glm::length(difference);
    const glm::vec3 next = differenceLength <= maxChange || differenceLength < kMinDifference ? wish : base + difference / differenceLength * maxChange;
    m_velocity = m_grounded ? next : next + m_up * vertical;
    if (m_grounded && m_input.jump) {
        m_velocity = (m_velocity - m_up * glm::dot(m_velocity, m_up)) + m_up * m_settings.jumpSpeed;
        m_grounded = false;
    } else {
        m_velocity += gravityVector * stepSeconds;
    }
}

void CharacterController::step(float stepSeconds) {
    const glm::vec3 gravityVector = gravity();
    alignUp(gravityVector, stepSeconds);
    accelerate(gravityVector, stepSeconds);
    const glm::vec3 previous = m_probe;
    move(m_velocity * stepSeconds);
    m_world.setPosition(m_entity, m_position);
    checkCross(previous);
    m_world.setPosition(m_entity, m_position);
    m_probe = probe();
}

void CharacterController::consumeImpacts() {
    for (const CharacterImpact& impact : m_pending) {
        glm::vec3 shove = impact.impulse;
        const float length = glm::length(shove);
        if (length > m_settings.impactMaxShove) shove *= m_settings.impactMaxShove / length;
        m_velocity += shove;
        const float kick = std::min(impact.speed * m_settings.impactKick, m_settings.impactKickMax);
        m_kick += glm::vec2(0.0f, kick);
        if (m_impactHook) m_impactHook(impact);
    }
    m_pending.clear();
}

void CharacterController::updateCamera() {
    Camera& camera = m_world.camera();
    camera.setOrientation(m_yaw + m_kick.x, std::clamp(m_pitch + m_kick.y, -kMaxCameraPitch, kMaxCameraPitch));
    if (m_rig.mode() == CameraMode::FirstPerson) {
        m_rig.update();
        return;
    }
    m_swept = false;
    m_rig.update();
    const float distance = m_rig.currentDistance();
    int cell = m_cell;
    if (m_swept && m_lastCast.crossings > 0 && distance > 0.0f) {
        const glm::vec3 forward = m_rig.forward();
        const glm::vec3 sideways = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 shoulder = glm::length(sideways) > kMinDirection ? glm::normalize(sideways) : glm::vec3(0.0f);
        const glm::vec3 anchor = eye() + shoulder * m_rig.settings().shoulderOffset;
        const PortalCast trace = portalSweep(m_portals, m_world, anchor, -forward, distance, 0.0f, m_cell, {}, false);
        camera.setPos(trace.point);
        const glm::vec3 viewForward = -trace.direction;
        camera.setOrientation(yawOf(viewForward), glm::degrees(std::asin(glm::clamp(viewForward.y, -1.0f, 1.0f))));
        cell = trace.cell;
    }
    m_portals.setCameraCell(cell);
}

void CharacterController::update(float dt) {
    if (!m_world.isAlive(m_entity) || !(dt > 0.0f) || !std::isfinite(dt)) return;
    consumeImpacts();
    m_probe = probe();
    const float substepLength = m_settings.substep > kMinSubstep ? m_settings.substep : kMinSubstep;
    const int substeps = static_cast<int>(std::clamp(std::ceil(dt / substepLength), 1.0f, static_cast<float>(kMaxSubsteps)));
    const float stepSeconds = dt / static_cast<float>(substeps);
    for (int substep = 0; substep < substeps; ++substep) step(stepSeconds);
    m_input.jump = false;
    m_kick *= std::exp(-m_settings.kickDecay * dt);
    updateCamera();
}
