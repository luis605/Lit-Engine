module;

#include <algorithm>
#include <cmath>
#include <vector>

module Engine.Character;

import Engine.camera;

namespace {
glm::vec3 rotate(const glm::mat4& m, const glm::vec3& v) { return glm::vec3(m * glm::vec4(v, 0.0f)); }
glm::vec3 place(const glm::mat4& m, const glm::vec3& v) { return glm::vec3(m * glm::vec4(v, 1.0f)); }

glm::vec3 flatForward(float yawDegrees) {
    const float y = glm::radians(yawDegrees);
    return glm::vec3(std::cos(y), 0.0f, std::sin(y));
}

float yawOf(const glm::vec3& f) { return glm::degrees(std::atan2(f.z, f.x)); }
}  // namespace

CharacterController::CharacterController(World& world, PortalSystem& portals, const PhysicsSettings& physics, CharacterSettings settings, CameraRigSettings rig)
    : m_world(world), m_portals(portals), m_physics(physics), m_s(settings), m_rig(world, rig) {
    EntityDesc desc;
    desc.name = "Character";
    m_entity = world.create(desc);
    desc.name = "Character Collider";
    desc.parent = m_entity;
    desc.position = glm::vec3(0.0f, m_s.height * 0.5f, 0.0f);
    m_collider = world.create(desc);
    world.add<BoxCollider>(m_collider, glm::vec3(m_s.radius, m_s.height * 0.5f, m_s.radius));
    m_rig.setTarget(m_entity);
    m_rig.setCollisionQuery([this](const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> std::optional<float> {
        const PhysicsFilter skip = [this](EntityHandle e) { return filtered(e); };
        m_lastCast = portalSweep(m_portals, m_world, origin, direction, maxDistance, m_rig.settings().collisionRadius, m_cell, skip, true);
        m_swept = true;
        if (!m_lastCast.hit) return std::nullopt;
        return m_lastCast.distance;
    });
    m_subscription = world.events().subscribe<Collision>([this](const Collision& c) {
        const bool first = c.a == m_collider;
        if (!first && !(c.b == m_collider)) return;
        if (c.closingSpeed < m_s.impactMinSpeed) return;
        const EntityHandle other = first ? c.b : c.a;
        const RigidBody* rb = m_world.get<RigidBody>(other);
        if (!rb || rb->isStatic) return;
        CharacterImpact impact;
        impact.source = other;
        impact.direction = first ? -c.normal : c.normal;
        impact.speed = c.closingSpeed;
        impact.impulse = impact.direction * (c.closingSpeed * rb->mass / m_s.mass);
        m_pending.push_back(impact);
    });
}

CharacterController::~CharacterController() {
    m_world.events().unsubscribe(m_subscription);
    if (m_world.isAlive(m_entity)) m_world.destroy(m_entity);
}

void CharacterController::spawn(const glm::vec3& feet, float yawDegrees, float pitchDegrees) {
    m_position = feet;
    m_velocity = glm::vec3(0.0f);
    m_yaw = yawDegrees;
    m_pitch = pitchDegrees;
    m_cell = std::max(m_portals.cellAt(feet + m_up * (m_s.height * 0.5f)), 0);
    m_world.setPosition(m_entity, m_position);
    m_probe = probe();
    m_portals.setCameraCell(m_cell);
    updateCamera();
}

void CharacterController::look(float yawDeltaDegrees, float pitchDeltaDegrees) {
    m_yaw += yawDeltaDegrees;
    m_pitch = std::clamp(m_pitch + pitchDeltaDegrees, -m_rig.settings().maxPitch, m_rig.settings().maxPitch);
}

glm::vec3 CharacterController::gravity() const {
    if (!m_portals.active()) return m_physics.gravity;
    return m_portals.sampleParams(m_cell, m_position + m_up * (m_s.height * 0.5f)).gravity;
}

glm::vec3 CharacterController::eye() const { return m_position + glm::vec3(0.0f, m_rig.settings().eyeHeight, 0.0f); }

glm::vec3 CharacterController::probe() const {
    if (m_rig.mode() != CameraMode::FirstPerson) return eye();
    const float yaw = glm::radians(m_yaw);
    const float pitch = glm::radians(m_pitch);
    const glm::vec3 f(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
    return eye() + f * m_world.camera().getNearPlane();
}

bool CharacterController::filtered(EntityHandle e) const {
    if (e == m_collider || e == m_entity || m_portals.isGhost(e)) return true;
    const RigidBody* rb = const_cast<World&>(m_world).get<RigidBody>(e);
    return rb && !rb->isStatic;
}

std::vector<PhysicsContact> CharacterController::gather(const glm::vec3& position) {
    const float r = m_s.radius;
    const glm::vec3 a = position + m_up * r;
    const glm::vec3 b = position + m_up * (m_s.height - r);
    const PhysicsFilter skip = [this](EntityHandle e) { return filtered(e); };
    std::vector<PhysicsContact> out;
    for (PhysicsContact& c : physicsOverlapCapsule(m_world, a, b, r, skip)) {
        if (c.depth > 1.0e-4f) out.push_back(c);
    }
    if (!m_portals.active()) return out;
    const glm::vec3 center = (a + b) * 0.5f;
    const float reach = m_s.height * 0.5f + r;
    for (int id : m_portals.portalsOf(m_cell)) {
        const Portal& p = m_portals.portals()[static_cast<size_t>(id)];
        const glm::vec3 q = center - p.center();
        const float side = glm::dot(q, p.normal());
        if (side < -r || side > reach) continue;
        if (std::abs(glm::dot(q, p.right())) > p.half.x + r || std::abs(glm::dot(q, p.up())) > p.half.y + reach) continue;
        const Portal& exit = m_portals.portals()[static_cast<size_t>(p.partner)];
        const glm::mat3 back = glm::transpose(glm::mat3(p.transform));
        for (const PhysicsContact& c : physicsOverlapCapsule(m_world, place(p.transform, a), place(p.transform, b), r, skip)) {
            if (c.depth <= 1.0e-4f || glm::dot(c.normal, exit.normal()) > 0.7f) continue;
            out.push_back({c.entity, back * c.normal, c.depth});
        }
    }
    return out;
}

bool CharacterController::resolve(glm::vec3& position, bool* wall) {
    const float cosSlope = std::cos(glm::radians(m_s.slopeLimit));
    bool grounded = false;
    for (int i = 0; i < 8; ++i) {
        const std::vector<PhysicsContact> contacts = gather(position);
        if (contacts.empty()) break;
        const PhysicsContact* deepest = &contacts[0];
        for (const PhysicsContact& c : contacts) {
            if (c.depth > deepest->depth) deepest = &c;
        }
        const glm::vec3 n = deepest->normal;
        position += n * deepest->depth;
        const float vn = glm::dot(m_velocity, n);
        if (vn < 0.0f) m_velocity -= n * vn;
        const float un = glm::dot(n, m_up);
        if (un >= cosSlope) {
            grounded = true;
            m_groundNormal = n;
        } else if (wall && std::abs(un) < 0.3f) {
            *wall = true;
        }
    }
    return grounded;
}

bool CharacterController::tryStep(const glm::vec3& start, const glm::vec3& delta) {
    const glm::vec3 horizontal = delta - m_up * glm::dot(delta, m_up);
    const glm::vec3 lifted = start + m_up * m_s.stepHeight;
    if (!gather(lifted).empty()) return false;
    const glm::vec3 target = lifted + horizontal;
    if (!gather(target).empty()) return false;
    const PhysicsFilter skip = [this](EntityHandle e) { return filtered(e); };
    const glm::vec3 base = target + m_up * m_s.radius;
    const auto hit = physicsSweepSphere(m_world, base, -m_up, m_s.stepHeight + 0.02f, m_s.radius, skip);
    glm::vec3 landed = target;
    if (hit) landed -= m_up * hit->distance;
    else return false;
    if (glm::dot(hit->normal, m_up) < std::cos(glm::radians(m_s.slopeLimit))) return false;
    if (!gather(landed).empty()) return false;
    m_position = landed;
    m_grounded = true;
    m_groundNormal = hit->normal;
    return true;
}

void CharacterController::snapDown() {
    const PhysicsFilter skip = [this](EntityHandle e) { return filtered(e); };
    const auto hit = physicsSweepSphere(m_world, m_position + m_up * m_s.radius, -m_up, m_s.stepHeight, m_s.radius, skip);
    if (!hit || glm::dot(hit->normal, m_up) < std::cos(glm::radians(m_s.slopeLimit))) return;
    m_position -= m_up * hit->distance;
    m_grounded = true;
    m_groundNormal = hit->normal;
    const float vn = glm::dot(m_velocity, m_up);
    if (vn < 0.0f) m_velocity -= m_up * vn;
}

void CharacterController::move(const glm::vec3& delta) {
    const bool was = m_grounded;
    const glm::vec3 start = m_position;
    m_position += delta;
    bool wall = false;
    m_grounded = resolve(m_position, &wall);
    if (was && wall && !m_grounded) {
        const glm::vec3 keepPosition = m_position;
        const glm::vec3 keepVelocity = m_velocity;
        if (!tryStep(start, delta)) {
            m_position = keepPosition;
            m_velocity = keepVelocity;
        }
    }
    if (was && !m_grounded && glm::dot(m_velocity, m_up) <= 0.0f) snapDown();
}

void CharacterController::checkCross(const glm::vec3& previousProbe) {
    if (!m_portals.active()) return;
    glm::vec3 moved = probe();
    int cell = m_cell;
    const int id = m_portals.stepPoint(moved, cell, previousProbe, true);
    if (id < 0) {
        const int resolved = m_portals.resolveCell(m_cell, probe());
        if (resolved != m_cell) {
            m_cell = resolved;
            m_portals.setCameraCell(resolved);
        }
        return;
    }
    const glm::mat4& t = m_portals.portals()[static_cast<size_t>(id)].transform;
    m_position = place(t, m_position);
    m_velocity = rotate(t, m_velocity);
    m_up = glm::normalize(rotate(t, m_up));
    m_yaw = yawOf(rotate(t, flatForward(m_yaw)));
    m_cell = cell;
    ++m_crossings;
    m_portals.setCameraCell(cell);
}

void CharacterController::step(float h) {
    const glm::vec3 g = gravity();
    if (glm::length(g) > 1.0e-4f) {
        const glm::vec3 target = -glm::normalize(g);
        m_up = glm::normalize(glm::mix(m_up, target, 1.0f - std::exp(-m_s.upResponse * h)));
    }
    float vertical = glm::dot(m_velocity, m_up);
    glm::vec3 horizontal = m_velocity - m_up * vertical;
    glm::vec3 forward = flatForward(m_yaw);
    forward = forward - m_up * glm::dot(forward, m_up);
    forward = glm::length(forward) > 1.0e-5f ? glm::normalize(forward) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, m_up));
    glm::vec3 wish = (forward * m_input.move.y + right * m_input.move.x);
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    wish *= m_s.speed * (m_input.sprint ? m_s.sprintScale : 1.0f);
    if (m_grounded) wish -= m_groundNormal * glm::dot(wish, m_groundNormal);
    const glm::vec3 base = m_grounded ? m_velocity : horizontal;
    const glm::vec3 diff = wish - base;
    const float rate = (m_grounded ? m_s.acceleration : m_s.airAcceleration) * h;
    const float len = glm::length(diff);
    const glm::vec3 next = len <= rate || len < 1.0e-6f ? wish : base + diff / len * rate;
    m_velocity = m_grounded ? next : next + m_up * vertical;
    if (m_grounded && m_input.jump) {
        m_velocity = (m_velocity - m_up * glm::dot(m_velocity, m_up)) + m_up * m_s.jumpSpeed;
        m_grounded = false;
    } else {
        m_velocity += g * h;
    }
    const glm::vec3 previous = m_probe;
    move(m_velocity * h);
    m_world.setPosition(m_entity, m_position);
    checkCross(previous);
    m_world.setPosition(m_entity, m_position);
    m_probe = probe();
}

void CharacterController::consumeImpacts() {
    for (const CharacterImpact& impact : m_pending) {
        glm::vec3 shove = impact.impulse;
        const float len = glm::length(shove);
        if (len > m_s.impactMaxShove) shove *= m_s.impactMaxShove / len;
        m_velocity += shove;
        const float kick = std::min(impact.speed * m_s.impactKick, m_s.impactKickMax);
        m_kick += glm::vec2(0.0f, kick);
        if (m_impactHook) m_impactHook(impact);
    }
    m_pending.clear();
}

void CharacterController::updateCamera() {
    Camera& camera = m_world.camera();
    camera.setOrientation(m_yaw + m_kick.x, std::clamp(m_pitch + m_kick.y, -89.0f, 89.0f));
    if (m_rig.mode() == CameraMode::FirstPerson) {
        m_rig.update();
        return;
    }
    m_swept = false;
    m_rig.update();
    const float distance = m_rig.currentDistance();
    int cell = m_cell;
    if (m_swept && m_lastCast.crossings > 0 && distance > 0.0f) {
        const glm::vec3 fwd = m_rig.forward();
        const glm::vec3 anchor = eye() + glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f))) * m_rig.settings().shoulderOffset;
        const PortalCast trace = portalSweep(m_portals, m_world, anchor, -fwd, distance, 0.0f, m_cell, {}, false);
        camera.setPos(trace.point);
        const glm::vec3 f = -trace.direction;
        camera.setOrientation(yawOf(f), glm::degrees(std::asin(glm::clamp(f.y, -1.0f, 1.0f))));
        cell = trace.cell;
    }
    m_portals.setCameraCell(cell);
}

void CharacterController::update(float dt) {
    if (!m_world.isAlive(m_entity) || dt <= 0.0f) return;
    consumeImpacts();
    m_probe = probe();
    const int n = std::clamp(static_cast<int>(std::ceil(dt / m_s.substep)), 1, 16);
    const float h = dt / static_cast<float>(n);
    for (int i = 0; i < n; ++i) step(h);
    m_input.jump = false;
    const float decay = std::exp(-m_s.kickDecay * dt);
    m_kick *= decay;
    updateCamera();
}
