module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr int kAxisX = 0;
constexpr int kAxisY = 1;
constexpr int kAxisZ = 2;
constexpr float kDominantNormal = 0.9f;

struct Opening {
    glm::vec2 low{0.0f};
    glm::vec2 high{0.0f};
};

struct FaceId {
    int axis = 0;
    float sign = 1.0f;
    bool operator==(const FaceId&) const = default;
};

struct FaceOpening {
    FaceId face;
    Opening opening;
};

int planeAxisA(int axis) { return axis == kAxisX ? kAxisY : kAxisX; }
int planeAxisB(int axis) { return axis == kAxisZ ? kAxisY : kAxisZ; }

std::vector<FaceOpening> openingsOf(const Room& room) {
    std::vector<FaceOpening> result;
    for (const PairSpec& pair : portalPairs()) {
        for (const PortalEnd* end : {&pair.a, &pair.b}) {
            if (end->cell != room.name) continue;
            const glm::mat4 pose = portalPose(end->position, end->eulerDegrees);
            const glm::vec3 normal(pose[2]);
            const glm::vec3 right(pose[0]);
            const glm::vec3 up(pose[1]);
            int axis = kAxisX;
            for (int candidate = 1; candidate < 3; ++candidate) {
                if (std::abs(normal[candidate]) > std::abs(normal[axis])) axis = candidate;
            }
            if (std::abs(normal[axis]) < kDominantNormal) continue;
            const int a = planeAxisA(axis);
            const int b = planeAxisB(axis);
            const auto extent = [&](int component) { return std::abs(right[component]) * end->halfSize.x + std::abs(up[component]) * end->halfSize.y; };
            Opening opening;
            opening.low = glm::vec2(end->position[a] - extent(a), end->position[b] - extent(b));
            opening.high = glm::vec2(end->position[a] + extent(a), end->position[b] + extent(b));
            result.push_back({{axis, normal[axis] > 0.0f ? -1.0f : 1.0f}, opening});
        }
    }
    return result;
}

void slabBounds(const Room& room, const FaceId& face, glm::vec3& low, glm::vec3& high) {
    low = interiorMin(room);
    high = interiorMax(room);
    const glm::vec3 inner = face.sign > 0.0f ? high : low;
    if (face.axis == kAxisY) {
        low.x -= kWallThickness;
        high.x += kWallThickness;
        low.z -= kWallThickness;
        high.z += kWallThickness;
    } else if (face.axis == kAxisX) {
        low.z -= kWallThickness;
        high.z += kWallThickness;
    }
    const float innerFace = inner[face.axis];
    low[face.axis] = face.sign > 0.0f ? innerFace : innerFace - kWallThickness;
    high[face.axis] = face.sign > 0.0f ? innerFace + kWallThickness : innerFace;
}

void emitSlab(const Context& context, const std::string& name, uint32_t surface, uint32_t shader, int axis, const glm::vec3& low, const glm::vec3& high, std::vector<Opening> openings) {
    const int a = planeAxisA(axis);
    const int b = planeAxisB(axis);
    const auto piece = [&](float a0, float a1, float b0, float b1) {
        glm::vec3 pieceLow = low;
        glm::vec3 pieceHigh = high;
        pieceLow[a] = a0;
        pieceHigh[a] = a1;
        pieceLow[b] = b0;
        pieceHigh[b] = b1;
        if (a1 - a0 > kMinSlab && b1 - b0 > kMinSlab) context.block(name, surface, pieceLow, pieceHigh, shader);
    };
    std::ranges::sort(openings, [](const Opening& left, const Opening& right) { return left.low.x < right.low.x; });
    float cursor = low[a];
    for (const Opening& opening : openings) {
        const float holeA0 = std::max(opening.low.x, low[a]);
        const float holeA1 = std::min(opening.high.x, high[a]);
        const float holeB0 = std::max(opening.low.y, low[b]);
        const float holeB1 = std::min(opening.high.y, high[b]);
        piece(cursor, holeA0, low[b], high[b]);
        piece(holeA0, holeA1, low[b], holeB0);
        piece(holeA0, holeA1, holeB1, high[b]);
        cursor = holeA1;
    }
    piece(cursor, high[a], low[b], high[b]);
}

}

void buildShell(const Context& context, const Room& room, const RoomStyle& style, uint32_t floorShader) {
    const uint32_t wallMaterial = context.surfaced(Finish::Plaster, style.wall, 0.8f, {.tileMeters = 2.5f, .normalStrength = 0.4f});
    const uint32_t floorMaterial = context.surfaced(style.floorFinish, style.floor, style.floorRoughness, {.tileMeters = style.floorTileMeters});
    const uint32_t ceilingMaterial = context.surfaced(Finish::Plaster, style.ceiling, 0.9f, {.tileMeters = 3.0f, .normalStrength = 0.4f});
    const std::vector<FaceOpening> openings = openingsOf(room);
    const FaceId faces[] = {{kAxisY, -1.0f}, {kAxisY, 1.0f}, {kAxisX, 1.0f}, {kAxisX, -1.0f}, {kAxisZ, 1.0f}, {kAxisZ, -1.0f}};
    for (const FaceId& face : faces) {
        glm::vec3 low;
        glm::vec3 high;
        slabBounds(room, face, low, high);
        std::vector<Opening> faceOpenings;
        for (const FaceOpening& candidate : openings) {
            if (candidate.face == face) faceOpenings.push_back(candidate.opening);
        }
        const bool isFloor = face.axis == kAxisY && face.sign < 0.0f;
        const bool isCeiling = face.axis == kAxisY && face.sign > 0.0f;
        const uint32_t surface = isFloor ? floorMaterial : isCeiling ? ceilingMaterial : wallMaterial;
        emitSlab(context, room.name + (isFloor ? " floor" : isCeiling ? " ceiling" : " wall"), surface, isFloor ? floorShader : 0u, face.axis, low, high, faceOpenings);
    }
}

}
