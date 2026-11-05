module;

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <vector>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;

namespace {

constexpr size_t kMaxSavedJoints = 64;
constexpr size_t kMaxSavedLegs = 64;

void writeHandle(std::ostream& out, EntityHandle handle) { out << handle.index << ' '; }

EntityHandle readHandle(std::istream& in, const LoadContext& context) {
    uint32_t index = 0;
    in >> index;
    return index == INVALID_ENTITY ? NULL_ENTITY : context.resolve(index);
}

void writeChain(const IkChain& chain, std::ostream& out) {
    out << chain.joints.size() << ' ';
    for (EntityHandle joint : chain.joints) writeHandle(out, joint);
    out << chain.limits.size() << ' ';
    for (const IkJointLimit& limit : chain.limits) out << int(limit.hinge) << ' ' << limit.minAngle << ' ' << limit.maxAngle << ' ' << limit.cone << ' ';
    writeHandle(out, chain.target);
    out << int(chain.useTargetPoint) << ' ' << chain.targetPoint.x << ' ' << chain.targetPoint.y << ' ' << chain.targetPoint.z << ' ';
    writeHandle(out, chain.pole);
    out << int(chain.usePolePoint) << ' ' << chain.polePoint.x << ' ' << chain.polePoint.y << ' ' << chain.polePoint.z << ' ';
    out << chain.weight << ' ' << chain.iterations << ' ' << chain.tolerance << ' ' << int(chain.algorithm) << ' ' << int(chain.enabled);
}

std::optional<IkChain> readChain(std::istream& in, const LoadContext& context) {
    IkChain chain;
    size_t jointCount = 0;
    if (!(in >> jointCount) || jointCount > kMaxSavedJoints) return std::nullopt;
    for (size_t index = 0; index < jointCount; ++index) chain.joints.push_back(readHandle(in, context));
    size_t limitCount = 0;
    if (!(in >> limitCount) || limitCount > kMaxSavedJoints) return std::nullopt;
    for (size_t index = 0; index < limitCount; ++index) {
        IkJointLimit limit;
        int hinge = 0;
        if (!(in >> hinge >> limit.minAngle >> limit.maxAngle >> limit.cone)) return std::nullopt;
        limit.hinge = hinge != 0;
        chain.limits.push_back(limit);
    }
    chain.target = readHandle(in, context);
    int flag = 0;
    if (!(in >> flag >> chain.targetPoint.x >> chain.targetPoint.y >> chain.targetPoint.z)) return std::nullopt;
    chain.useTargetPoint = flag != 0;
    chain.pole = readHandle(in, context);
    if (!(in >> flag >> chain.polePoint.x >> chain.polePoint.y >> chain.polePoint.z)) return std::nullopt;
    chain.usePolePoint = flag != 0;
    int algorithm = 0;
    int enabled = 1;
    if (!(in >> chain.weight >> chain.iterations >> chain.tolerance >> algorithm >> enabled)) return std::nullopt;
    chain.algorithm = static_cast<IkAlgorithm>(algorithm);
    chain.enabled = enabled != 0;
    return chain;
}

void writeWalker(const Walker& walker, std::ostream& out) {
    writeHandle(out, walker.body);
    out << walker.legs.size() << ' ';
    for (const WalkerLeg& leg : walker.legs) {
        writeHandle(out, leg.chain);
        writeHandle(out, leg.foot);
    }
    out << walker.stride << ' ' << walker.stepHeight << ' ' << walker.bodyHeight << ' ' << walker.maxSpeed << ' ' << walker.acceleration << ' ' << walker.turnRate << ' ' << walker.arrivalRadius << ' ' << walker.slowRadius << ' ' << walker.swingSpeed << ' ';
    out << walker.minSwingTime << ' ' << walker.maxSwingTime << ' ' << walker.heightSmoothing << ' ' << walker.tiltSmoothing << ' ' << walker.maxTiltDegrees << ' ' << walker.probeUp << ' ' << walker.probeDown << ' ' << walker.maxSimultaneous << ' ' << int(walker.enabled);
}

std::optional<Walker> readWalker(std::istream& in, const LoadContext& context) {
    Walker walker;
    walker.body = readHandle(in, context);
    size_t legCount = 0;
    if (!(in >> legCount) || legCount > kMaxSavedLegs) return std::nullopt;
    for (size_t index = 0; index < legCount; ++index) {
        WalkerLeg leg;
        leg.chain = readHandle(in, context);
        leg.foot = readHandle(in, context);
        walker.legs.push_back(leg);
    }
    int enabled = 1;
    if (!(in >> walker.stride >> walker.stepHeight >> walker.bodyHeight >> walker.maxSpeed >> walker.acceleration >> walker.turnRate >> walker.arrivalRadius >> walker.slowRadius >> walker.swingSpeed)) return std::nullopt;
    if (!(in >> walker.minSwingTime >> walker.maxSwingTime >> walker.heightSmoothing >> walker.tiltSmoothing >> walker.maxTiltDegrees >> walker.probeUp >> walker.probeDown >> walker.maxSimultaneous >> enabled)) return std::nullopt;
    walker.enabled = enabled != 0;
    return walker;
}

}

void registerIkComponents(World& world) {
    world.registerComponent<IkChain>("IkChain", writeChain, readChain);
    world.registerComponent<Walker>("Walker", writeWalker, readWalker);
}
