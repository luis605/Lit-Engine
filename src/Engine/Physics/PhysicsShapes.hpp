#pragma once

// Shape math shared by the Engine.Physics implementation units. Included after `module Engine.Physics;`.

namespace phys_detail {

inline constexpr float kEps = 1.0e-7f;

struct Triangle {
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
};

inline glm::vec3 closestOnTriangle(const glm::vec3& p, const Triangle& t) {
    const glm::vec3 ab = t.b - t.a;
    const glm::vec3 ac = t.c - t.a;
    const glm::vec3 ap = p - t.a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return t.a;
    const glm::vec3 bp = p - t.b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return t.b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return t.a + ab * (d1 / (d1 - d3));
    const glm::vec3 cp = p - t.c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return t.c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return t.a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denominator = 1.0f / (va + vb + vc);
    return t.a + ab * (vb * denominator) + ac * (vc * denominator);
}

// Two sided ray / triangle (direction need not be unit; distance is in units of direction).
inline std::optional<float> rayTriangle(const glm::vec3& origin, const glm::vec3& direction, const Triangle& t) {
    const glm::vec3 e1 = t.b - t.a;
    const glm::vec3 e2 = t.c - t.a;
    const glm::vec3 p = glm::cross(direction, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < kEps) return std::nullopt;
    const float inv = 1.0f / det;
    const glm::vec3 s = origin - t.a;
    const float u = glm::dot(s, p) * inv;
    if (u < -1.0e-5f || u > 1.0f + 1.0e-5f) return std::nullopt;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(direction, q) * inv;
    if (v < -1.0e-5f || u + v > 1.0f + 1.0e-5f) return std::nullopt;
    const float distance = glm::dot(e2, q) * inv;
    if (distance < 0.0f) return std::nullopt;
    return distance;
}

inline glm::vec3 triangleNormal(const Triangle& t) {
    const glm::vec3 n = glm::cross(t.b - t.a, t.c - t.a);
    const float length = glm::length(n);
    return length < kEps ? glm::vec3(0.0f, 1.0f, 0.0f) : n / length;
}

inline glm::vec3 closestOnSegment(const glm::vec3& start, const glm::vec3& end, const glm::vec3& point) {
    const glm::vec3 segment = end - start;
    const float lengthSquared = glm::dot(segment, segment);
    if (lengthSquared < 1.0e-12f) return start;
    return start + segment * glm::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
}

// Closest points between segments p1-q1 and p2-q2 (Ericson).
inline void closestSegmentSegment(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    if (a <= 1.0e-12f && e <= 1.0e-12f) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= 1.0e-12f) {
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= 1.0e-12f) {
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;
            s = denom > 1.0e-12f ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = glm::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = glm::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

struct Contact {
    glm::vec3 normal;
    float depth;
};

enum class ShapeKind : uint8_t { Sphere, Box, Capsule, Hull };

// One primitive of a body (offset from the body position, world axes).
struct Part {
    ShapeKind kind = ShapeKind::Sphere;
    glm::vec3 offset{0.0f};
    float radius = 0.0f;
    glm::vec3 half{0.0f};
    float halfHeight = 0.0f;
    const ConvexHullData* hull = nullptr;
    glm::mat3 xf{1.0f};
};

inline float partBound(const Part& part) {
    switch (part.kind) {
    case ShapeKind::Sphere: return part.radius;
    case ShapeKind::Box: return glm::length(part.half);
    case ShapeKind::Capsule: return part.halfHeight + part.radius;
    case ShapeKind::Hull: {
        float best = 0.0f;
        if (part.hull) {
            for (const glm::vec3& p : part.hull->points) best = std::max(best, glm::length(part.xf * p));
        }
        return best;
    }
    }
    return 0.0f;
}

struct WorldHull {
    struct Face {
        glm::vec3 n;
        float d;
        std::vector<uint32_t> idx;
    };
    std::vector<glm::vec3> v;
    std::vector<Face> faces;
    std::vector<glm::vec3> edgeDirs;
    glm::vec3 center{0.0f};
};

inline WorldHull makeWorldHull(const ConvexHullData& hull, const glm::mat3& xf, const glm::vec3& at) {
    WorldHull out;
    out.v.reserve(hull.points.size());
    for (const glm::vec3& p : hull.points) out.v.push_back(xf * p + at);
    glm::vec3 sum(0.0f);
    for (const glm::vec3& p : out.v) sum += p;
    out.center = out.v.empty() ? at : sum / static_cast<float>(out.v.size());
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(xf));
    for (const ConvexHullData::Face& face : hull.faces) {
        WorldHull::Face f;
        f.n = glm::normalize(normalMatrix * face.normal);
        f.idx = face.indices;
        f.d = glm::dot(f.n, out.v[face.indices[0]]);
        out.faces.push_back(std::move(f));
    }
    for (const auto& [i, j] : hull.edges) {
        const glm::vec3 e = out.v[j] - out.v[i];
        const float length = glm::length(e);
        if (length > 1.0e-6f) out.edgeDirs.push_back(e / length);
    }
    return out;
}

inline WorldHull makeBoxHull(const glm::vec3& c, const glm::vec3& h) {
    WorldHull out;
    out.center = c;
    for (int i = 0; i < 8; ++i) out.v.push_back(c + glm::vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z));
    for (int axis = 0; axis < 3; ++axis) {
        glm::vec3 dir(0.0f);
        dir[axis] = 1.0f;
        out.edgeDirs.push_back(dir);
        for (int sign = -1; sign <= 1; sign += 2) {
            WorldHull::Face f;
            f.n = dir * static_cast<float>(sign);
            f.d = glm::dot(f.n, c) + h[axis];
            out.faces.push_back(std::move(f));
        }
    }
    // Box faces carry corner indices only for the closest point fans; build them from the bit patterns.
    for (WorldHull::Face& f : out.faces) {
        int axis = std::abs(f.n.x) > 0.5f ? 0 : (std::abs(f.n.y) > 0.5f ? 1 : 2);
        const int bit = 1 << axis;
        const bool positive = f.n[axis] > 0.0f;
        std::vector<uint32_t> ring;
        for (uint32_t i = 0; i < 8; ++i) {
            if (((i & bit) != 0) == positive) ring.push_back(i);
        }
        // order around the face: sort by angle
        const glm::vec3 fc = c + f.n * h[axis];
        glm::vec3 u = std::abs(f.n.x) > 0.5f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 w = glm::normalize(glm::cross(f.n, u));
        u = glm::cross(w, f.n);
        std::sort(ring.begin(), ring.end(), [&](uint32_t l, uint32_t r) {
            const glm::vec3 pl = out.v[l] - fc;
            const glm::vec3 pr = out.v[r] - fc;
            return std::atan2(glm::dot(pl, w), glm::dot(pl, u)) < std::atan2(glm::dot(pr, w), glm::dot(pr, u));
        });
        f.idx = std::move(ring);
    }
    return out;
}

inline WorldHull partHull(const Part& part, const glm::vec3& center) {
    if (part.kind == ShapeKind::Box) return makeBoxHull(center, part.half);
    return makeWorldHull(*part.hull, part.xf, center);
}

struct HullPoint {
    glm::vec3 point;
    glm::vec3 normal;
    float dist;
};

// dist is signed: negative inside. normal points out of the hull toward p.
inline HullPoint closestOnHull(const WorldHull& hull, const glm::vec3& p) {
    float maxSeparation = -1.0e30f;
    int best = 0;
    for (size_t i = 0; i < hull.faces.size(); ++i) {
        const float s = glm::dot(hull.faces[i].n, p) - hull.faces[i].d;
        if (s > maxSeparation) {
            maxSeparation = s;
            best = static_cast<int>(i);
        }
    }
    if (maxSeparation < 0.0f) return {p - hull.faces[static_cast<size_t>(best)].n * maxSeparation, hull.faces[static_cast<size_t>(best)].n, maxSeparation};
    float bestDistance = 1.0e30f;
    glm::vec3 bestPoint = p;
    for (const WorldHull::Face& face : hull.faces) {
        for (size_t i = 1; i + 1 < face.idx.size(); ++i) {
            const glm::vec3 q = closestOnTriangle(p, {hull.v[face.idx[0]], hull.v[face.idx[i]], hull.v[face.idx[i + 1]]});
            const float d = glm::length(p - q);
            if (d < bestDistance) {
                bestDistance = d;
                bestPoint = q;
            }
        }
    }
    const glm::vec3 away = p - bestPoint;
    const glm::vec3 normal = bestDistance > 1.0e-6f ? away / bestDistance : hull.faces[static_cast<size_t>(best)].n;
    return {bestPoint, normal, bestDistance};
}

struct SatAxis {
    glm::vec3 dir;
    bool edge;
};

// Separating axis test. Normal points from A to B (by center). Edge axes must beat face axes by 5 percent.
inline std::optional<Contact> satContact(const std::vector<glm::vec3>& va, const std::vector<glm::vec3>& vb, const glm::vec3& ca, const glm::vec3& cb, const std::vector<SatAxis>& axes) {
    float best = 1.0e30f;
    glm::vec3 bestAxis(0.0f, 1.0f, 0.0f);
    bool haveFace = false;
    for (int pass = 0; pass < 2; ++pass) {
        for (const SatAxis& axis : axes) {
            if ((pass == 0) == axis.edge) continue;
            const float length = glm::length(axis.dir);
            if (length < 1.0e-4f) continue;
            const glm::vec3 l = axis.dir / length;
            float minA = 1.0e30f, maxA = -1.0e30f, minB = 1.0e30f, maxB = -1.0e30f;
            for (const glm::vec3& p : va) {
                const float d = glm::dot(l, p);
                minA = std::min(minA, d);
                maxA = std::max(maxA, d);
            }
            for (const glm::vec3& p : vb) {
                const float d = glm::dot(l, p);
                minB = std::min(minB, d);
                maxB = std::max(maxB, d);
            }
            const float overlap = std::min(maxA - minB, maxB - minA);
            if (overlap <= 0.0f) return std::nullopt;
            const bool better = axis.edge ? overlap < best * 0.95f - 1.0e-5f : overlap < best;
            if (better || (!haveFace && !axis.edge)) {
                best = overlap;
                bestAxis = glm::dot(l, cb - ca) >= 0.0f ? l : -l;
                haveFace = haveFace || !axis.edge;
            }
        }
    }
    return Contact{bestAxis, best};
}

inline std::optional<Contact> hullHull(const WorldHull& a, const WorldHull& b) {
    std::vector<SatAxis> axes;
    for (const auto& f : a.faces) axes.push_back({f.n, false});
    for (const auto& f : b.faces) axes.push_back({f.n, false});
    for (const glm::vec3& ea : a.edgeDirs) {
        for (const glm::vec3& eb : b.edgeDirs) axes.push_back({glm::cross(ea, eb), true});
    }
    return satContact(a.v, b.v, a.center, b.center, axes);
}

// Normal from the triangle toward the hull.
inline std::optional<Contact> hullTriangle(const WorldHull& hull, const Triangle& t) {
    std::vector<SatAxis> axes;
    const glm::vec3 tn = triangleNormal(t);
    axes.push_back({tn, false});
    for (const auto& f : hull.faces) axes.push_back({f.n, false});
    const glm::vec3 edges[3] = {t.b - t.a, t.c - t.b, t.a - t.c};
    for (const glm::vec3& eh : hull.edgeDirs) {
        for (const glm::vec3& et : edges) axes.push_back({glm::cross(eh, et), true});
    }
    const std::vector<glm::vec3> tv = {t.a, t.b, t.c};
    return satContact(tv, hull.v, (t.a + t.b + t.c) / 3.0f, hull.center, axes);
}

inline std::optional<Contact> sphereSphere(const glm::vec3& c1, float r1, const glm::vec3& c2, float r2) {
    const glm::vec3 offset = c2 - c1;
    const float reach = r1 + r2;
    const float d2 = glm::dot(offset, offset);
    if (d2 >= reach * reach) return std::nullopt;
    const float d = std::sqrt(d2);
    return Contact{d > 1.0e-6f ? offset / d : glm::vec3(0.0f, 1.0f, 0.0f), reach - d};
}

// Normal from sphere to box.
inline std::optional<Contact> sphereBox(const glm::vec3& sc, float radius, const glm::vec3& bc, const glm::vec3& half) {
    const glm::vec3 local = sc - bc;
    const glm::vec3 closest = glm::clamp(local, -half, half);
    const glm::vec3 difference = local - closest;
    const float d2 = glm::dot(difference, difference);
    if (d2 > 1.0e-10f) {
        if (d2 >= radius * radius) return std::nullopt;
        const float d = std::sqrt(d2);
        return Contact{-difference / d, radius - d};
    }
    int axis = 0;
    float shallowest = half.x - std::abs(local.x);
    const float py = half.y - std::abs(local.y);
    const float pz = half.z - std::abs(local.z);
    if (py < shallowest) {
        shallowest = py;
        axis = 1;
    }
    if (pz < shallowest) {
        shallowest = pz;
        axis = 2;
    }
    glm::vec3 outward(0.0f);
    outward[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{-outward, shallowest + radius};
}

inline std::optional<Contact> boxBox(const glm::vec3& c1, const glm::vec3& h1, const glm::vec3& c2, const glm::vec3& h2) {
    const glm::vec3 offset = c2 - c1;
    const glm::vec3 overlap = h1 + h2 - glm::abs(offset);
    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return std::nullopt;
    int axis = 0;
    if (overlap.y < overlap[axis]) axis = 1;
    if (overlap.z < overlap[axis]) axis = 2;
    glm::vec3 normal(0.0f);
    normal[axis] = offset[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{normal, overlap[axis]};
}

// Normal from capsule to box.
inline std::optional<Contact> capsuleBox(const glm::vec3& s0, const glm::vec3& s1, float radius, const glm::vec3& bc, const glm::vec3& half) {
    const glm::vec3 low = bc - half;
    const glm::vec3 high = bc + half;
    glm::vec3 onSegment = closestOnSegment(s0, s1, bc);
    glm::vec3 onBox = glm::clamp(onSegment, low, high);
    for (int i = 0; i < 4; ++i) {
        onSegment = closestOnSegment(s0, s1, onBox);
        onBox = glm::clamp(onSegment, low, high);
    }
    const glm::vec3 separation = onSegment - onBox;
    const float d2 = glm::dot(separation, separation);
    if (d2 > 1.0e-10f) {
        if (d2 >= radius * radius) return std::nullopt;
        const float d = std::sqrt(d2);
        return Contact{-separation / d, radius - d};
    }
    const glm::vec3 local = onSegment - bc;
    int axis = 0;
    float shallowest = half.x - std::abs(local.x);
    for (int candidate = 1; candidate < 3; ++candidate) {
        const float penetration = half[candidate] - std::abs(local[candidate]);
        if (penetration < shallowest) {
            shallowest = penetration;
            axis = candidate;
        }
    }
    glm::vec3 outward(0.0f);
    outward[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{-outward, shallowest + radius};
}

// Normal from capsule (or sphere when s0 == s1) to hull.
inline std::optional<Contact> capsuleHull(const glm::vec3& s0, const glm::vec3& s1, float radius, const WorldHull& hull) {
    glm::vec3 p = closestOnSegment(s0, s1, hull.center);
    HullPoint hp = closestOnHull(hull, p);
    for (int i = 0; i < 6 && hp.dist >= 0.0f; ++i) {
        p = closestOnSegment(s0, s1, hp.point);
        hp = closestOnHull(hull, p);
    }
    if (hp.dist >= radius) return std::nullopt;
    return Contact{-hp.normal, radius - hp.dist};
}

inline void capsuleSegment(const Part& part, const glm::vec3& c, glm::vec3& s0, glm::vec3& s1) {
    s0 = c - glm::vec3(0.0f, part.halfHeight, 0.0f);
    s1 = c + glm::vec3(0.0f, part.halfHeight, 0.0f);
}

// Contact between two parts (centers are world). Normal points a -> b.
inline std::optional<Contact> collideParts(const Part& pa, const glm::vec3& ca, const Part& pb, const glm::vec3& cb) {
    if (pa.kind > pb.kind) {
        auto flipped = collideParts(pb, cb, pa, ca);
        if (flipped) flipped->normal = -flipped->normal;
        return flipped;
    }
    glm::vec3 a0, a1, b0, b1;
    switch (pa.kind) {
    case ShapeKind::Sphere:
        switch (pb.kind) {
        case ShapeKind::Sphere: return sphereSphere(ca, pa.radius, cb, pb.radius);
        case ShapeKind::Box: return sphereBox(ca, pa.radius, cb, pb.half);
        case ShapeKind::Capsule:
            capsuleSegment(pb, cb, b0, b1);
            return sphereSphere(ca, pa.radius, closestOnSegment(b0, b1, ca), pb.radius);
        case ShapeKind::Hull: {
            const WorldHull h = partHull(pb, cb);
            auto c = capsuleHull(ca, ca, pa.radius, h);
            return c;
        }
        }
        break;
    case ShapeKind::Box:
        switch (pb.kind) {
        case ShapeKind::Box: return boxBox(ca, pa.half, cb, pb.half);
        case ShapeKind::Capsule: {
            capsuleSegment(pb, cb, b0, b1);
            auto c = capsuleBox(b0, b1, pb.radius, ca, pa.half);
            if (c) c->normal = -c->normal;
            return c;
        }
        case ShapeKind::Hull: return hullHull(makeBoxHull(ca, pa.half), partHull(pb, cb));
        default: break;
        }
        break;
    case ShapeKind::Capsule:
        capsuleSegment(pa, ca, a0, a1);
        switch (pb.kind) {
        case ShapeKind::Capsule: {
            capsuleSegment(pb, cb, b0, b1);
            glm::vec3 c1, c2;
            closestSegmentSegment(a0, a1, b0, b1, c1, c2);
            return sphereSphere(c1, pa.radius, c2, pb.radius);
        }
        case ShapeKind::Hull: return capsuleHull(a0, a1, pa.radius, partHull(pb, cb));
        default: break;
        }
        break;
    case ShapeKind::Hull:
        if (pb.kind == ShapeKind::Hull) return hullHull(partHull(pa, ca), partHull(pb, cb));
        break;
    }
    return std::nullopt;
}

// World bounds (center +- extent) of a part at a position.
inline void partBounds(const Part& part, const glm::vec3& c, glm::vec3& lo, glm::vec3& hi) {
    switch (part.kind) {
    case ShapeKind::Sphere:
        lo = c - part.radius;
        hi = c + part.radius;
        break;
    case ShapeKind::Box:
        lo = c - part.half;
        hi = c + part.half;
        break;
    case ShapeKind::Capsule:
        lo = c - glm::vec3(part.radius, part.halfHeight + part.radius, part.radius);
        hi = c + glm::vec3(part.radius, part.halfHeight + part.radius, part.radius);
        break;
    case ShapeKind::Hull:
        lo = glm::vec3(1.0e30f);
        hi = glm::vec3(-1.0e30f);
        if (part.hull) {
            for (const glm::vec3& p : part.hull->points) {
                const glm::vec3 w = part.xf * p + c;
                lo = glm::min(lo, w);
                hi = glm::max(hi, w);
            }
        }
        break;
    }
}

// Surface contact of a capsule segment against one world triangle. Normal points from the triangle toward the capsule.
inline std::optional<Contact> capsuleTriangle(const glm::vec3& s0, const glm::vec3& s1, float radius, const Triangle& t) {
    const glm::vec3 axis = s1 - s0;
    const float length = glm::length(axis);
    const glm::vec3 tn = triangleNormal(t);
    if (length > 1.0e-6f) {
        const auto hit = rayTriangle(s0, axis / length, t);
        if (hit && *hit <= length) {
            const float da = glm::dot(tn, s0 - t.a);
            const float db = glm::dot(tn, s1 - t.a);
            const float s = std::abs(da) >= std::abs(db) ? (da >= 0.0f ? 1.0f : -1.0f) : (db >= 0.0f ? 1.0f : -1.0f);
            const bool opposite = (da > 0.0f) != (db > 0.0f);
            return Contact{tn * s, radius + (opposite ? std::min(std::abs(da), std::abs(db)) : 0.0f)};
        }
    }
    glm::vec3 p = closestOnSegment(s0, s1, (t.a + t.b + t.c) / 3.0f);
    glm::vec3 q = closestOnTriangle(p, t);
    for (int i = 0; i < 4; ++i) {
        p = closestOnSegment(s0, s1, q);
        q = closestOnTriangle(p, t);
    }
    const glm::vec3 away = p - q;
    const float d = glm::length(away);
    if (d >= radius) return std::nullopt;
    return Contact{d > 1.0e-6f ? away / d : tn, radius - d};
}

// Surface contact of a part against one world triangle. Normal points from the triangle toward the part.
inline std::optional<Contact> partTriangle(const Part& part, const glm::vec3& c, const Triangle& t, const WorldHull* hull) {
    switch (part.kind) {
    case ShapeKind::Sphere: {
        const glm::vec3 cp = closestOnTriangle(c, t);
        const glm::vec3 away = c - cp;
        const float d = glm::length(away);
        if (d >= part.radius) return std::nullopt;
        return Contact{d > 1.0e-6f ? away / d : triangleNormal(t), part.radius - d};
    }
    case ShapeKind::Capsule: {
        glm::vec3 s0, s1;
        capsuleSegment(part, c, s0, s1);
        return capsuleTriangle(s0, s1, part.radius, t);
    }
    case ShapeKind::Box:
    case ShapeKind::Hull: return hull ? hullTriangle(*hull, t) : std::nullopt;
    }
    return std::nullopt;
}

}  // namespace phys_detail
