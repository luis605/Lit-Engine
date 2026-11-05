module;

#include <cmath>
#include <cstddef>
#include <functional>
#include <typeindex>
#include <typeinfo>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;

namespace {

constexpr float kCrossSize = 0.08f;
constexpr int kRingSegments = 16;
constexpr float kPi = 3.14159265358979f;
const glm::vec4 kBoneColor(0.2f, 1.0f, 0.4f, 1.0f);
const glm::vec4 kTargetColor(1.0f, 0.8f, 0.1f, 1.0f);
const glm::vec4 kPoleColor(0.3f, 0.6f, 1.0f, 1.0f);
const glm::vec4 kPlantedColor(1.0f, 0.3f, 0.2f, 1.0f);
const glm::vec4 kSwingColor(0.9f, 0.4f, 1.0f, 1.0f);
const glm::vec4 kStrideColor(0.6f, 0.6f, 0.6f, 0.7f);

void drawCross(const IkLineSink& line, const glm::vec3& center, const glm::vec4& color) {
    line(center - glm::vec3(kCrossSize, 0.0f, 0.0f), center + glm::vec3(kCrossSize, 0.0f, 0.0f), color);
    line(center - glm::vec3(0.0f, kCrossSize, 0.0f), center + glm::vec3(0.0f, kCrossSize, 0.0f), color);
    line(center - glm::vec3(0.0f, 0.0f, kCrossSize), center + glm::vec3(0.0f, 0.0f, kCrossSize), color);
}

void drawRing(const IkLineSink& line, const glm::vec3& center, float radius, const glm::vec4& color) {
    for (int segment = 0; segment < kRingSegments; ++segment) {
        const float from = 2.0f * kPi * static_cast<float>(segment) / static_cast<float>(kRingSegments);
        const float to = 2.0f * kPi * static_cast<float>(segment + 1) / static_cast<float>(kRingSegments);
        line(center + glm::vec3(std::cos(from), 0.0f, std::sin(from)) * radius, center + glm::vec3(std::cos(to), 0.0f, std::sin(to)) * radius, color);
    }
}

void drawChain(World& world, const IkChain& chain, const IkLineSink& line) {
    for (size_t index = 0; index + 1 < chain.joints.size(); ++index) {
        if (!world.isAlive(chain.joints[index]) || !world.isAlive(chain.joints[index + 1])) return;
        line(world.getWorldPosition(chain.joints[index]), world.getWorldPosition(chain.joints[index + 1]), kBoneColor);
    }
    if (world.isAlive(chain.target)) drawCross(line, world.getWorldPosition(chain.target), kTargetColor);
    else if (chain.useTargetPoint) drawCross(line, chain.targetPoint, kTargetColor);
    if (chain.usePolePoint && !chain.joints.empty() && world.isAlive(chain.joints.front())) line(world.getWorldPosition(chain.joints.front()), chain.polePoint, kPoleColor);
}

void drawWalker(World& world, const Walker& walker, const IkLineSink& line) {
    if (!world.isAlive(walker.body)) return;
    const glm::vec3 bodyPosition = world.getWorldPosition(walker.body);
    if (walker.hasMoveTarget) line(bodyPosition, walker.moveTarget, kTargetColor);
    for (const WalkerLeg& leg : walker.legs) {
        if (leg.swinging) {
            line(leg.swingFrom, leg.position, kSwingColor);
            line(leg.position, leg.swingTo, kSwingColor);
            drawCross(line, leg.swingTo, kSwingColor);
        } else {
            drawCross(line, leg.planted, kPlantedColor);
        }
        glm::vec3 home = bodyPosition + world.getWorldRotation(walker.body) * glm::vec3(leg.localRest.x, 0.0f, leg.localRest.z);
        home.y = leg.planted.y;
        drawRing(line, home, walker.stride, kStrideColor);
    }
}

}

void drawIkDebug(World& world, const IkLineSink& line) {
    world.view<IkChain>([&](EntityHandle, IkChain& chain) { drawChain(world, chain, line); });
    world.view<Walker>([&](EntityHandle, Walker& walker) { drawWalker(world, walker, line); });
}
