module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

module Engine.Physics;

namespace {
constexpr float kEpsilon = 1.0e-7f;
constexpr int kSweepIterations = 128;
constexpr int kSweepBisect = 14;

struct Triangle {
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
};

glm::vec3 cellVertex(const HeightfieldData& field, int x, int z) { return glm::vec3(static_cast<float>(x) * field.cellSize, field.at(x, z), static_cast<float>(z) * field.cellSize); }

void cellTriangles(const HeightfieldData& field, int i, int j, Triangle& first, Triangle& second) {
    const glm::vec3 p00 = cellVertex(field, i, j);
    const glm::vec3 p10 = cellVertex(field, i + 1, j);
    const glm::vec3 p01 = cellVertex(field, i, j + 1);
    const glm::vec3 p11 = cellVertex(field, i + 1, j + 1);
    first = {p00, p01, p11};
    second = {p00, p11, p10};
}

glm::vec3 upNormal(const Triangle& triangle) {
    glm::vec3 normal = glm::cross(triangle.b - triangle.a, triangle.c - triangle.a);
    const float length = glm::length(normal);
    if (length < kEpsilon) return glm::vec3(0.0f, 1.0f, 0.0f);
    normal /= length;
    return normal.y < 0.0f ? -normal : normal;
}

glm::vec3 closestOnTriangle(const glm::vec3& p, const Triangle& t) {
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

std::optional<float> rayTriangle(const glm::vec3& origin, const glm::vec3& direction, const Triangle& t) {
    const glm::vec3 e1 = t.b - t.a;
    const glm::vec3 e2 = t.c - t.a;
    const glm::vec3 p = glm::cross(direction, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < kEpsilon) return std::nullopt;
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

struct Nearest {
    float distance = std::numeric_limits<float>::max();
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool found = false;
};

Nearest nearestSurface(const HeightfieldData& field, const glm::vec3& center, float reach) {
    Nearest best;
    const int cellsX = field.samplesX - 1;
    const int cellsZ = field.samplesZ - 1;
    const int i0 = std::max(0, static_cast<int>(std::floor((center.x - reach) / field.cellSize)));
    const int i1 = std::min(cellsX - 1, static_cast<int>(std::floor((center.x + reach) / field.cellSize)));
    const int j0 = std::max(0, static_cast<int>(std::floor((center.z - reach) / field.cellSize)));
    const int j1 = std::min(cellsZ - 1, static_cast<int>(std::floor((center.z + reach) / field.cellSize)));
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            Triangle triangles[2];
            cellTriangles(field, i, j, triangles[0], triangles[1]);
            for (const Triangle& triangle : triangles) {
                const glm::vec3 point = closestOnTriangle(center, triangle);
                const float distance = glm::length(center - point);
                if (distance < best.distance) {
                    best.distance = distance;
                    best.point = point;
                    best.normal = upNormal(triangle);
                    best.found = true;
                }
            }
        }
    }
    return best;
}

float signedDistance(const HeightfieldData& field, const glm::vec3& center, float reach, glm::vec3& normal) {
    const Nearest nearest = nearestSurface(field, center, reach);
    if (!nearest.found) {
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
        return reach;
    }
    const bool inside = heightfieldContains(field, center.x, center.z) && center.y < heightfieldHeightAt(field, center.x, center.z);
    glm::vec3 away = center - nearest.point;
    const float length = glm::length(away);
    normal = (length > 1.0e-5f && !inside) ? away / length : nearest.normal;
    return inside ? -nearest.distance : nearest.distance;
}
}

void HeightfieldData::updateBounds() {
    if (heights.empty()) {
        minHeight = maxHeight = 0.0f;
        return;
    }
    const auto [low, high] = std::minmax_element(heights.begin(), heights.end());
    minHeight = *low;
    maxHeight = *high;
}

bool heightfieldContains(const HeightfieldData& field, float x, float z) { return field.valid() && x >= 0.0f && z >= 0.0f && x <= field.extentX() && z <= field.extentZ(); }

float heightfieldHeightAt(const HeightfieldData& field, float x, float z) {
    if (!field.valid()) return 0.0f;
    const float u = std::clamp(x, 0.0f, field.extentX()) / field.cellSize;
    const float v = std::clamp(z, 0.0f, field.extentZ()) / field.cellSize;
    const int i = std::min(static_cast<int>(u), field.samplesX - 2);
    const int j = std::min(static_cast<int>(v), field.samplesZ - 2);
    const float fx = u - static_cast<float>(i);
    const float fz = v - static_cast<float>(j);
    const float h00 = field.at(i, j);
    const float h10 = field.at(i + 1, j);
    const float h01 = field.at(i, j + 1);
    const float h11 = field.at(i + 1, j + 1);
    if (fz >= fx) return h00 + (h11 - h01) * fx + (h01 - h00) * fz;
    return h00 + (h10 - h00) * fx + (h11 - h10) * fz;
}

glm::vec3 heightfieldNormalAt(const HeightfieldData& field, float x, float z) {
    if (!field.valid()) return glm::vec3(0.0f, 1.0f, 0.0f);
    const float u = std::clamp(x, 0.0f, field.extentX()) / field.cellSize;
    const float v = std::clamp(z, 0.0f, field.extentZ()) / field.cellSize;
    const int i = std::min(static_cast<int>(u), field.samplesX - 2);
    const int j = std::min(static_cast<int>(v), field.samplesZ - 2);
    Triangle triangles[2];
    cellTriangles(field, i, j, triangles[0], triangles[1]);
    const float fx = u - static_cast<float>(i);
    const float fz = v - static_cast<float>(j);
    return upNormal(fz >= fx ? triangles[0] : triangles[1]);
}

std::optional<HeightfieldHit> heightfieldRaycast(const HeightfieldData& field, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    if (!field.valid()) return std::nullopt;
    const float length = glm::length(direction);
    if (length < kEpsilon) return std::nullopt;
    const glm::vec3 dir = direction / length;

    const glm::vec3 low(0.0f, field.minHeight - 1.0e-3f, 0.0f);
    const glm::vec3 high(field.extentX(), field.maxHeight + 1.0e-3f, field.extentZ());
    float enter = 0.0f;
    float leave = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < kEpsilon) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return std::nullopt;
            continue;
        }
        float nearT = (low[axis] - origin[axis]) / dir[axis];
        float farT = (high[axis] - origin[axis]) / dir[axis];
        if (nearT > farT) std::swap(nearT, farT);
        enter = std::max(enter, nearT);
        leave = std::min(leave, farT);
        if (enter > leave) return std::nullopt;
    }
    const glm::vec3 start = origin + dir * enter;
    if (enter == 0.0f && heightfieldContains(field, origin.x, origin.z) && origin.y < heightfieldHeightAt(field, origin.x, origin.z)) {
        return HeightfieldHit{0.0f, heightfieldNormalAt(field, origin.x, origin.z)};
    }

    const int cellsX = field.samplesX - 1;
    const int cellsZ = field.samplesZ - 1;
    int i = std::clamp(static_cast<int>(std::floor(start.x / field.cellSize)), 0, cellsX - 1);
    int j = std::clamp(static_cast<int>(std::floor(start.z / field.cellSize)), 0, cellsZ - 1);
    const int stepX = dir.x > 0.0f ? 1 : -1;
    const int stepZ = dir.z > 0.0f ? 1 : -1;
    const float inf = std::numeric_limits<float>::max();
    const float deltaX = std::abs(dir.x) < kEpsilon ? inf : field.cellSize / std::abs(dir.x);
    const float deltaZ = std::abs(dir.z) < kEpsilon ? inf : field.cellSize / std::abs(dir.z);
    const auto firstBoundary = [&](float position, float d, int cell, int step) {
        if (std::abs(d) < kEpsilon) return inf;
        const float boundary = static_cast<float>(step > 0 ? cell + 1 : cell) * field.cellSize;
        return enter + (boundary - position) / d;
    };
    float nextX = firstBoundary(start.x, dir.x, i, stepX);
    float nextZ = firstBoundary(start.z, dir.z, j, stepZ);

    for (;;) {
        Triangle triangles[2];
        cellTriangles(field, i, j, triangles[0], triangles[1]);
        std::optional<HeightfieldHit> best;
        for (const Triangle& triangle : triangles) {
            const auto hit = rayTriangle(origin, dir, triangle);
            if (hit && *hit <= leave && (!best || *hit < best->distance)) best = HeightfieldHit{*hit, upNormal(triangle)};
        }
        if (best) return best;
        if (nextX < nextZ) {
            if (nextX > leave) break;
            i += stepX;
            nextX += deltaX;
        } else {
            if (nextZ > leave) break;
            j += stepZ;
            nextZ += deltaZ;
        }
        if (i < 0 || i >= cellsX || j < 0 || j >= cellsZ) break;
    }
    return std::nullopt;
}

std::optional<HeightfieldContact> heightfieldSphereContact(const HeightfieldData& field, const glm::vec3& center, float radius) {
    if (!field.valid()) return std::nullopt;
    if (center.x < -radius || center.z < -radius || center.x > field.extentX() + radius || center.z > field.extentZ() + radius) return std::nullopt;
    if (center.y - radius > field.maxHeight) return std::nullopt;
    glm::vec3 normal;
    const float distance = signedDistance(field, center, radius, normal);
    if (distance >= radius) return std::nullopt;
    return HeightfieldContact{normal, radius - distance, center - normal * distance};
}

std::optional<HeightfieldHit> heightfieldSweepSphere(const HeightfieldData& field, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius) {
    if (!field.valid()) return std::nullopt;
    const float length = glm::length(direction);
    if (length < kEpsilon) return std::nullopt;
    const glm::vec3 dir = direction / length;
    const float reach = radius + std::max(field.cellSize, radius);
    const float minStep = std::max(1.0e-3f, field.cellSize * 0.02f);
    const auto clearance = [&](float t, glm::vec3& normal) {
        const glm::vec3 p = origin + dir * t;
        if (p.x < -reach || p.z < -reach || p.x > field.extentX() + reach || p.z > field.extentZ() + reach) {
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
            return reach;
        }
        return signedDistance(field, p, reach, normal) - radius;
    };

    glm::vec3 normal;
    float t = 0.0f;
    float previous = 0.0f;
    float gap = clearance(0.0f, normal);
    if (gap < 0.0f) return HeightfieldHit{0.0f, normal};
    for (int iteration = 0; iteration < kSweepIterations; ++iteration) {
        const float step = std::max(gap, minStep);
        previous = t;
        t += step;
        if (t > maxDistance) {
            t = maxDistance;
            gap = clearance(t, normal);
            if (gap > 0.0f) return std::nullopt;
        } else {
            gap = clearance(t, normal);
        }
        if (gap <= 1.0e-4f) {
            float lo = previous;
            float hi = t;
            glm::vec3 hitNormal = normal;
            for (int bisect = 0; bisect < kSweepBisect; ++bisect) {
                const float mid = 0.5f * (lo + hi);
                glm::vec3 midNormal;
                if (clearance(mid, midNormal) > 1.0e-4f) lo = mid;
                else {
                    hi = mid;
                    hitNormal = midNormal;
                }
            }
            return HeightfieldHit{hi, hitNormal};
        }
        if (t >= maxDistance) return std::nullopt;
    }
    return std::nullopt;
}
