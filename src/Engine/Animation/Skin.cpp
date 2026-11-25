module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Engine.Skin;

import Engine.glm;
import Engine.mesh;

namespace {

constexpr float kEpsilon = 1.0e-6f;
constexpr float kPiValue = 3.14159265358979f;

glm::vec3 vertexPosition(const std::vector<float>& v, size_t i) { return glm::vec3(v[i * kVertexFloats], v[i * kVertexFloats + 1], v[i * kVertexFloats + 2]); }
glm::vec3 vertexNormal(const std::vector<float>& v, size_t i) { return glm::vec3(v[i * kVertexFloats + 3], v[i * kVertexFloats + 4], v[i * kVertexFloats + 5]); }

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEpsilon ? v / len : fallback;
}

}

int DynamicMesh::findMorph(const std::string& name) const {
    for (size_t i = 0; i < morphs.size(); ++i) {
        if (morphs[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int DynamicMesh::findJoint(const std::string& name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

size_t normalizeSkinWeights(std::vector<SkinVertex>& skin, uint32_t jointCount) {
    size_t repaired = 0;
    for (SkinVertex& vertex : skin) {
        float sum = 0.0f;
        bool changed = false;
        for (uint32_t i = 0; i < kMaxSkinInfluences; ++i) {
            float& weight = vertex.weights[i];
            if (!(weight > 0.0f) || !std::isfinite(weight)) {
                if (weight != 0.0f) changed = true;
                weight = 0.0f;
            }
            if (vertex.joints[i] >= jointCount) {
                if (weight != 0.0f) changed = true;
                vertex.joints[i] = 0;
                weight = 0.0f;
            }
            sum += weight;
        }
        if (!std::isfinite(sum)) {
            // Huge finite weights overflow the sum (inf): the inverse became 0 and left a vertex with all weights zero (found by bent,
            // ext_phys_anim.bent skin_normalize_repairs_any_weights). Scale by the largest weight first.
            const float largest = std::max(std::max(vertex.weights[0], vertex.weights[1]), std::max(vertex.weights[2], vertex.weights[3]));
            sum = 0.0f;
            for (float& weight : vertex.weights) {
                weight /= largest;
                sum += weight;
            }
            changed = true;
        }
        if (sum <= kEpsilon) {
            vertex.weights = {1.0f, 0.0f, 0.0f, 0.0f};
            vertex.joints[0] = 0;
            ++repaired;
            continue;
        }
        if (std::fabs(sum - 1.0f) > 1.0e-4f) changed = true;
        const float inverse = 1.0f / sum;
        for (float& weight : vertex.weights) weight *= inverse;
        if (changed) ++repaired;
    }
    return repaired;
}

bool validateDynamicMesh(const DynamicMesh& mesh, std::string* error) {
    auto fail = [&](std::string message) {
        if (error) *error = std::move(message);
        return false;
    };
    const size_t count = mesh.vertexCount();
    if (mesh.rest.vertices.size() % kVertexFloats != 0) return fail("vertex array is not a multiple of 8 floats");
    if (count == 0 || mesh.rest.indices.empty()) return fail("mesh has no geometry");
    for (unsigned int index : mesh.rest.indices) {
        if (index >= count) return fail("index out of range");
    }
    if (!mesh.skin.empty()) {
        if (mesh.skin.size() != count) return fail("skin weights do not match the vertex count");
        if (mesh.joints.empty() || mesh.joints.size() > kMaxSkinJoints) return fail(std::format("joint count {} outside 1..{}", mesh.joints.size(), kMaxSkinJoints));
    }
    if (mesh.morphs.size() > kMaxMorphTargets) return fail(std::format("more than {} morph targets", kMaxMorphTargets));
    for (const MorphTarget& target : mesh.morphs) {
        if (target.positionDelta.size() != count) return fail("morph '" + target.name + "' position deltas do not match the vertex count");
        if (!target.normalDelta.empty() && target.normalDelta.size() != count) return fail("morph '" + target.name + "' normal deltas do not match the vertex count");
    }
    return true;
}

void buildBonePalette(std::span<const glm::mat4> jointModel, std::span<const SkinJoint> joints, std::vector<glm::mat4>& palette) {
    palette.resize(joints.size());
    for (size_t i = 0; i < joints.size(); ++i) palette[i] = i < jointModel.size() ? jointModel[i] * joints[i].inverseBind : glm::mat4(1.0f);
}

SkinBounds buildSkinBounds(const DynamicMesh& mesh) {
    SkinBounds bounds;
    const size_t count = mesh.vertexCount();
    const glm::vec3 big(std::numeric_limits<float>::max());
    bounds.restMin = big;
    bounds.restMax = -big;
    bounds.boneMin.assign(mesh.joints.size(), big);
    bounds.boneMax.assign(mesh.joints.size(), -big);
    bounds.boneUsed.assign(mesh.joints.size(), 0);
    for (size_t i = 0; i < count; ++i) {
        const glm::vec3 p = vertexPosition(mesh.rest.vertices, i);
        bounds.restMin = glm::min(bounds.restMin, p);
        bounds.restMax = glm::max(bounds.restMax, p);
        if (!mesh.skinned()) continue;
        for (uint32_t k = 0; k < kMaxSkinInfluences; ++k) {
            if (mesh.skin[i].weights[k] <= 0.0f) continue;
            const uint32_t joint = std::min<uint32_t>(mesh.skin[i].joints[k], static_cast<uint32_t>(mesh.joints.size() - 1));
            bounds.boneMin[joint] = glm::min(bounds.boneMin[joint], p);
            bounds.boneMax[joint] = glm::max(bounds.boneMax[joint], p);
            bounds.boneUsed[joint] = 1;
        }
    }
    if (count == 0) bounds.restMin = bounds.restMax = glm::vec3(0.0f);
    for (const MorphTarget& target : mesh.morphs) {
        float reach = 0.0f;
        for (const glm::vec3& d : target.positionDelta) reach = std::max(reach, glm::length(d));
        bounds.morphReach.push_back(reach);
    }
    return bounds;
}

float deformReach(const DeformSettings& deform) {
    switch (deform.mode) {
        case DeformMode::Wave: return std::fabs(deform.amplitude) * 1.1f;
        case DeformMode::Cloth: return std::fabs(deform.amplitude) * 1.5f;
        case DeformMode::User: return std::fabs(deform.userBounds);
        default: return 0.0f;
    }
}

glm::vec4 computeDynamicBounds(const SkinBounds& bounds, std::span<const glm::mat4> palette, std::span<const float> morphWeights, const DeformSettings& deform) {
    float morph = 0.0f;
    for (size_t t = 0; t < morphWeights.size() && t < bounds.morphReach.size(); ++t) morph += std::fabs(morphWeights[t]) * bounds.morphReach[t];
    const glm::vec3 big(std::numeric_limits<float>::max());
    glm::vec3 low = big;
    glm::vec3 high = -big;
    auto grow = [&](const glm::vec3& boxMin, const glm::vec3& boxMax, const glm::mat4* matrix) {
        const glm::vec3 a = boxMin - glm::vec3(morph);
        const glm::vec3 b = boxMax + glm::vec3(morph);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? b.x : a.x, (corner & 2) ? b.y : a.y, (corner & 4) ? b.z : a.z);
            const glm::vec3 q = matrix ? glm::vec3(*matrix * glm::vec4(p, 1.0f)) : p;
            low = glm::min(low, q);
            high = glm::max(high, q);
        }
    };
    bool any = false;
    for (size_t j = 0; j < bounds.boneUsed.size(); ++j) {
        if (!bounds.boneUsed[j]) continue;
        any = true;
        const glm::mat4 identity(1.0f);
        grow(bounds.boneMin[j], bounds.boneMax[j], j < palette.size() ? &palette[j] : &identity);
    }
    if (!any) grow(bounds.restMin, bounds.restMax, nullptr);
    const glm::vec3 center = (low + high) * 0.5f;
    return glm::vec4(center, glm::length(high - low) * 0.5f + deformReach(deform));
}

glm::vec3 deformPoint(const DeformSettings& deform, const glm::vec3& position, const glm::vec3& normal) {
    if (deform.mode != DeformMode::Wave && deform.mode != DeformMode::Cloth) return position;
    const glm::vec3 direction = safeNormalize(deform.direction, glm::vec3(1.0f, 0.0f, 0.0f));
    const float phase = glm::dot(position, direction) * deform.frequency - deform.time * deform.speed;
    glm::vec3 out = position;
    if (deform.mode == DeformMode::Wave) {
        out += normal * (deform.amplitude * std::sin(phase));
        out.y += 0.25f * deform.amplitude * std::cos(phase);
    } else {
        const float hang = std::clamp((deform.pinY - position.y) / std::max(deform.pinRange, 1.0e-4f), 0.0f, 1.0f);
        const float amplitude = deform.amplitude * hang;
        out += normal * (amplitude * (std::sin(phase) + 0.5f * std::sin(2.3f * phase + 1.7f)));
    }
    return out;
}

void recomputeNormals(std::vector<float>& vertices, const std::vector<unsigned int>& indices) {
    const size_t count = vertices.size() / kVertexFloats;
    std::vector<glm::vec3> sum(count, glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const unsigned int a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= count || b >= count || c >= count) continue;
        const glm::vec3 pa = vertexPosition(vertices, a);
        const glm::vec3 n = glm::cross(vertexPosition(vertices, b) - pa, vertexPosition(vertices, c) - pa);
        sum[a] += n;
        sum[b] += n;
        sum[c] += n;
    }
    for (size_t i = 0; i < count; ++i) {
        const glm::vec3 n = safeNormalize(sum[i], vertexNormal(vertices, i));
        vertices[i * kVertexFloats + 3] = n.x;
        vertices[i * kVertexFloats + 4] = n.y;
        vertices[i * kVertexFloats + 5] = n.z;
    }
}

void deformMeshCpu(const DynamicMesh& mesh, std::span<const glm::mat4> palette, std::span<const float> morphWeights, const DeformSettings& deform, std::vector<float>& out) {
    const size_t count = mesh.vertexCount();
    out = mesh.rest.vertices;
    for (size_t i = 0; i < count; ++i) {
        glm::vec3 position = vertexPosition(mesh.rest.vertices, i);
        glm::vec3 normal = vertexNormal(mesh.rest.vertices, i);
        for (size_t t = 0; t < mesh.morphs.size() && t < morphWeights.size(); ++t) {
            const float weight = morphWeights[t];
            if (weight == 0.0f) continue;
            position += weight * mesh.morphs[t].positionDelta[i];
            if (!mesh.morphs[t].normalDelta.empty()) normal += weight * mesh.morphs[t].normalDelta[i];
        }
        if (mesh.skinned()) {
            glm::mat4 blend(0.0f);
            for (uint32_t k = 0; k < kMaxSkinInfluences; ++k) {
                const float weight = mesh.skin[i].weights[k];
                if (weight == 0.0f) continue;
                const uint32_t joint = mesh.skin[i].joints[k];
                blend = blend + (joint < palette.size() ? palette[joint] : glm::mat4(1.0f)) * weight;
            }
            position = glm::vec3(blend * glm::vec4(position, 1.0f));
            normal = glm::mat3(blend) * normal;
        }
        normal = safeNormalize(normal, glm::vec3(0.0f, 1.0f, 0.0f));
        position = deformPoint(deform, position, normal);
        out[i * kVertexFloats + 0] = position.x;
        out[i * kVertexFloats + 1] = position.y;
        out[i * kVertexFloats + 2] = position.z;
        out[i * kVertexFloats + 3] = normal.x;
        out[i * kVertexFloats + 4] = normal.y;
        out[i * kVertexFloats + 5] = normal.z;
    }
    if (deform.recomputeNormals && deform.mode != DeformMode::None) recomputeNormals(out, mesh.rest.indices);
}

namespace {

struct Builder {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    std::vector<SkinVertex> skin;

    unsigned int add(const glm::vec3& p, const glm::vec3& n, float u, float v, uint32_t jointA, uint32_t jointB, float weightB) {
        vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, v});
        SkinVertex s;
        s.joints = {jointA, jointB, 0, 0};
        s.weights = {1.0f - weightB, weightB, 0.0f, 0.0f};
        skin.push_back(s);
        return static_cast<unsigned int>(skin.size() - 1);
    }
    void triangle(unsigned int a, unsigned int b, unsigned int c) { indices.insert(indices.end(), {a, b, c}); }
};

struct Tube {
    std::vector<uint32_t> chain;
    std::vector<float> radii;
    bool capStart = false;
    bool capEnd = true;
};

void buildTube(Builder& builder, const std::vector<glm::vec3>& world, const Tube& tube, int segments, int ringsPerBone) {
    std::vector<unsigned int> firstOfRing;
    float length = 0.0f;
    for (size_t k = 0; k + 1 < tube.chain.size(); ++k) length += glm::length(world[tube.chain[k + 1]] - world[tube.chain[k]]);
    float travelled = 0.0f;
    for (size_t k = 0; k + 1 < tube.chain.size(); ++k) {
        const glm::vec3 a = world[tube.chain[k]];
        const glm::vec3 b = world[tube.chain[k + 1]];
        const glm::vec3 axis = safeNormalize(b - a, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 reference = std::fabs(axis.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 u = safeNormalize(glm::cross(axis, reference), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 w = glm::cross(axis, u);
        const int last = (k + 2 == tube.chain.size()) ? ringsPerBone : ringsPerBone - 1;
        for (int r = 0; r <= last; ++r) {
            const float f = static_cast<float>(r) / static_cast<float>(ringsPerBone);
            const glm::vec3 center = a + (b - a) * f;
            const float radius = tube.radii[k] + (tube.radii[k + 1] - tube.radii[k]) * f;
            const float v = (travelled + glm::length(b - a) * f) / std::max(length, kEpsilon);
            firstOfRing.push_back(static_cast<unsigned int>(builder.skin.size()));
            for (int s = 0; s < segments; ++s) {
                const float angle = 2.0f * kPiValue * static_cast<float>(s) / static_cast<float>(segments);
                const glm::vec3 radial = u * std::cos(angle) + w * std::sin(angle);
                builder.add(center + radial * radius, radial, static_cast<float>(s) / static_cast<float>(segments), v, tube.chain[k], tube.chain[k + 1], f);
            }
        }
        travelled += glm::length(b - a);
    }
    for (size_t ring = 0; ring + 1 < firstOfRing.size(); ++ring) {
        for (int s = 0; s < segments; ++s) {
            const unsigned int s1 = static_cast<unsigned int>((s + 1) % segments);
            const unsigned int a0 = firstOfRing[ring] + static_cast<unsigned int>(s), a1 = firstOfRing[ring] + s1;
            const unsigned int b0 = firstOfRing[ring + 1] + static_cast<unsigned int>(s), b1 = firstOfRing[ring + 1] + s1;
            builder.triangle(a0, b1, b0);
            builder.triangle(a0, a1, b1);
        }
    }
    auto cap = [&](unsigned int ringStart, const glm::vec3& center, const glm::vec3& normal, uint32_t joint, bool flip) {
        const unsigned int c = builder.add(center, normal, 0.5f, 0.5f, joint, joint, 0.0f);
        for (int s = 0; s < segments; ++s) {
            const unsigned int s1 = static_cast<unsigned int>((s + 1) % segments);
            if (flip) builder.triangle(c, ringStart + s1, ringStart + static_cast<unsigned int>(s));
            else builder.triangle(c, ringStart + static_cast<unsigned int>(s), ringStart + s1);
        }
    };
    const glm::vec3 startAxis = safeNormalize(world[tube.chain[1]] - world[tube.chain[0]], glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 endAxis = safeNormalize(world[tube.chain.back()] - world[tube.chain[tube.chain.size() - 2]], glm::vec3(0.0f, 1.0f, 0.0f));
    if (tube.capStart) cap(firstOfRing.front(), world[tube.chain.front()], -startAxis, tube.chain.front(), true);
    if (tube.capEnd) cap(firstOfRing.back(), world[tube.chain.back()], endAxis, tube.chain.back(), false);
}

void buildSphere(Builder& builder, const glm::vec3& center, float radius, uint32_t joint, int segments, int rings) {
    const unsigned int base = static_cast<unsigned int>(builder.skin.size());
    for (int r = 0; r <= rings; ++r) {
        const float phi = kPiValue * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * kPiValue * static_cast<float>(s) / static_cast<float>(segments);
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            builder.add(center + n * radius, n, static_cast<float>(s) / static_cast<float>(segments), static_cast<float>(r) / static_cast<float>(rings), joint, joint, 0.0f);
        }
    }
    const unsigned int stride = static_cast<unsigned int>(segments + 1);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const unsigned int a = base + static_cast<unsigned int>(r) * stride + static_cast<unsigned int>(s);
            const unsigned int b = a + stride;
            builder.triangle(a, a + 1, b);
            builder.triangle(a + 1, b + 1, b);
        }
    }
}

float gauss(const glm::vec3& d, float sigma) { return std::exp(-glm::dot(d, d) / (2.0f * sigma * sigma)); }

void addHeadMorphs(DynamicMesh& mesh, uint32_t headJoint, const glm::vec3& headCenter, float radius) {
    struct Spec {
        const char* name;
        glm::vec3 focus;
        glm::vec3 delta;
        float sigma;
        bool mirrorX;
    };
    const float R = radius;
    const Spec specs[] = {
        {"jawOpen", glm::vec3(0.0f, -0.65f, 0.55f) * R, glm::vec3(0.0f, -0.55f, -0.05f) * R, 0.55f * R, false},
        {"mouthSmileLeft", glm::vec3(0.35f, -0.4f, 0.85f) * R, glm::vec3(0.18f, 0.22f, 0.0f) * R, 0.28f * R, false},
        {"mouthSmileRight", glm::vec3(-0.35f, -0.4f, 0.85f) * R, glm::vec3(-0.18f, 0.22f, 0.0f) * R, 0.28f * R, false},
        {"eyeBlinkLeft", glm::vec3(0.38f, 0.22f, 0.88f) * R, glm::vec3(0.0f, -0.3f, -0.12f) * R, 0.24f * R, false},
        {"eyeBlinkRight", glm::vec3(-0.38f, 0.22f, 0.88f) * R, glm::vec3(0.0f, -0.3f, -0.12f) * R, 0.24f * R, false},
        {"browInnerUp", glm::vec3(0.0f, 0.6f, 0.8f) * R, glm::vec3(0.0f, 0.35f, 0.05f) * R, 0.3f * R, false},
        {"cheekPuff", glm::vec3(0.0f, -0.25f, 0.8f) * R, glm::vec3(0.0f), 0.8f * R, true},
    };
    const size_t count = mesh.vertexCount();
    for (const Spec& spec : specs) {
        MorphTarget target;
        target.name = spec.name;
        target.positionDelta.assign(count, glm::vec3(0.0f));
        for (size_t i = 0; i < count; ++i) {
            if (mesh.skin[i].joints[0] != headJoint || mesh.skin[i].weights[0] < 0.999f) continue;
            const glm::vec3 local = vertexPosition(mesh.rest.vertices, i) - headCenter;
            if (spec.mirrorX) {
                const glm::vec3 n = vertexNormal(mesh.rest.vertices, i);
                const float g = gauss(glm::vec3(std::fabs(local.x) - 0.55f * R, local.y + 0.2f * R, local.z - 0.4f * R), 0.35f * R);
                target.positionDelta[i] = n * (0.35f * R * g);
            } else {
                target.positionDelta[i] = spec.delta * gauss(local - spec.focus, spec.sigma);
            }
        }
        mesh.morphs.push_back(std::move(target));
    }
}

}

DynamicMesh makeProceduralCharacter(const CharacterOptions& options) {
    const float s = options.height / 1.8f;
    struct JointDef {
        const char* name;
        int parent;
        glm::vec3 local;
    };
    const JointDef defs[] = {
        {"hips", -1, {0.0f, 0.95f, 0.0f}},        {"spine", 0, {0.0f, 0.15f, 0.0f}},         {"chest", 1, {0.0f, 0.2f, 0.0f}},
        {"neck", 2, {0.0f, 0.2f, 0.0f}},          {"head", 3, {0.0f, 0.1f, 0.0f}},           {"shoulderL", 2, {0.2f, 0.1f, 0.0f}},
        {"elbowL", 5, {0.04f, -0.28f, 0.0f}},     {"wristL", 6, {0.02f, -0.26f, 0.0f}},      {"shoulderR", 2, {-0.2f, 0.1f, 0.0f}},
        {"elbowR", 8, {-0.04f, -0.28f, 0.0f}},    {"wristR", 9, {-0.02f, -0.26f, 0.0f}},     {"hipL", 0, {0.1f, -0.05f, 0.0f}},
        {"kneeL", 11, {0.0f, -0.45f, 0.0f}},      {"ankleL", 12, {0.0f, -0.42f, 0.0f}},      {"hipR", 0, {-0.1f, -0.05f, 0.0f}},
        {"kneeR", 14, {0.0f, -0.45f, 0.0f}},      {"ankleR", 15, {0.0f, -0.42f, 0.0f}},
    };
    DynamicMesh mesh;
    std::vector<glm::vec3> world;
    for (const JointDef& def : defs) {
        SkinJoint joint;
        joint.name = def.name;
        joint.parent = def.parent;
        joint.position = def.local * s;
        const glm::vec3 p = (def.parent >= 0 ? world[static_cast<size_t>(def.parent)] : glm::vec3(0.0f)) + joint.position;
        world.push_back(p);
        joint.inverseBind = glm::translate(glm::mat4(1.0f), -p);
        mesh.joints.push_back(std::move(joint));
    }
    Builder builder;
    const int seg = std::max(options.radialSegments, 3);
    const int rings = std::max(options.ringsPerBone, 1);
    const std::vector<Tube> tubes = {
        {{0, 1, 2}, {0.17f * s, 0.16f * s, 0.19f * s}, true, true},
        {{2, 3}, {0.07f * s, 0.06f * s}, false, false},
        {{5, 6, 7}, {0.065f * s, 0.055f * s, 0.045f * s}, true, true},
        {{8, 9, 10}, {0.065f * s, 0.055f * s, 0.045f * s}, true, true},
        {{11, 12, 13}, {0.09f * s, 0.07f * s, 0.05f * s}, true, true},
        {{14, 15, 16}, {0.09f * s, 0.07f * s, 0.05f * s}, true, true},
    };
    for (const Tube& tube : tubes) buildTube(builder, world, tube, seg, rings);
    const float headRadius = 0.12f * s;
    const glm::vec3 headCenter = world[4] + glm::vec3(0.0f, 0.08f * s, 0.0f);
    buildSphere(builder, headCenter, headRadius, 4, 16, 10);
    // The sphere's pole axis is +Y, longitude theta = 0 faces +X; rotate nothing: the face is on +Z (theta = pi/2).
    mesh.rest = Mesh(std::move(builder.vertices), std::move(builder.indices));
    mesh.skin = std::move(builder.skin);
    if (options.headMorphs) addHeadMorphs(mesh, 4, headCenter, headRadius);
    return mesh;
}

DynamicMesh makeClothSheet(float width, float height, int columns, int rows) {
    columns = std::max(columns, 1);
    rows = std::max(rows, 1);
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    for (int layer = 0; layer < 2; ++layer) {
        const float nz = layer == 0 ? 1.0f : -1.0f;
        for (int r = 0; r <= rows; ++r) {
            for (int c = 0; c <= columns; ++c) {
                const float u = static_cast<float>(c) / static_cast<float>(columns);
                const float v = static_cast<float>(r) / static_cast<float>(rows);
                vertices.insert(vertices.end(), {(u - 0.5f) * width, -v * height, 0.0f, 0.0f, 0.0f, nz, u, v});
            }
        }
    }
    const unsigned int stride = static_cast<unsigned int>(columns + 1);
    const unsigned int layerSize = stride * static_cast<unsigned int>(rows + 1);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const unsigned int a = static_cast<unsigned int>(r) * stride + static_cast<unsigned int>(c);
            const unsigned int b = a + 1, d = a + stride, e = d + 1;
            indices.insert(indices.end(), {a, d, b, b, d, e});
            indices.insert(indices.end(), {layerSize + a, layerSize + b, layerSize + d, layerSize + b, layerSize + e, layerSize + d});
        }
    }
    return DynamicMesh(Mesh(std::move(vertices), std::move(indices)));
}
