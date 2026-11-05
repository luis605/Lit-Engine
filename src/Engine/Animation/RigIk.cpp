module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

module Engine.Rig;

using namespace RigDetail;

namespace {
constexpr float EPSILON_BONE = 1.0e-6f;
constexpr float EPSILON_HINT = 1.0e-5f;
constexpr float REACH_MARGIN = 1.0e-4f;

const glm::quat IDENTITY_ROTATION(1.0f, 0.0f, 0.0f, 0.0f);

struct ModelScratch {
    ModelPose first;
    ModelPose second;
    ModelPose third;
};

ModelScratch& twoBoneScratch() {
    thread_local ModelScratch scratch;
    return scratch;
}

ModelScratch& lookAtScratch() {
    thread_local ModelScratch scratch;
    return scratch;
}

ModelScratch& footIkScratch() {
    thread_local ModelScratch scratch;
    return scratch;
}
}

bool solveTwoBone(const Skeleton& skeleton, Pose& pose, const TwoBoneChain& chain, const glm::vec3& targetModel, const std::optional<glm::vec3>& poleModel, float weight) {
    if (!validJoint(skeleton, chain.root) || !validJoint(skeleton, chain.mid) || !validJoint(skeleton, chain.tip)) return false;
    if (!poseCovers(skeleton, pose) || !finiteVec(targetModel) || (poleModel && !finiteVec(*poleModel)) || !std::isfinite(weight)) return false;
    ModelScratch& scratch = twoBoneScratch();
    ModelPose& before = scratch.first;
    computeModelPose(skeleton, pose, before);
    const glm::vec3 rootPosition = before.position[chain.root];
    const glm::vec3 midPosition = before.position[chain.mid];
    const glm::vec3 tipPosition = before.position[chain.tip];
    const float upperLength = glm::length(midPosition - rootPosition);
    const float lowerLength = glm::length(tipPosition - midPosition);
    if (upperLength < EPSILON_BONE || lowerLength < EPSILON_BONE) return false;
    const glm::vec3 toTarget = targetModel - rootPosition;
    const float rawDistance = glm::length(toTarget);
    if (rawDistance < EPSILON_BONE) return false;
    const float distance = std::clamp(rawDistance, std::fabs(upperLength - lowerLength) + REACH_MARGIN, upperLength + lowerLength - REACH_MARGIN);
    const glm::vec3 direction = toTarget / rawDistance;
    glm::vec3 hint = poleModel ? (*poleModel - rootPosition) : (midPosition - rootPosition);
    hint = hint - direction * glm::dot(hint, direction);
    if (glm::length(hint) < EPSILON_HINT) {
        hint = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::length(hint) < EPSILON_HINT) hint = glm::cross(direction, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    hint = glm::normalize(hint);
    const float cosineRoot = std::clamp((upperLength * upperLength + distance * distance - lowerLength * lowerLength) / (2.0f * upperLength * distance), -1.0f, 1.0f);
    const float sineRoot = std::sqrt(std::max(0.0f, 1.0f - cosineRoot * cosineRoot));
    const glm::vec3 newMid = rootPosition + direction * (upperLength * cosineRoot) + hint * (upperLength * sineRoot);

    const glm::quat originalRoot = pose.joints[chain.root].rotation;
    const glm::quat originalMid = pose.joints[chain.mid].rotation;
    const glm::quat rootRotation = rotationBetween(midPosition - rootPosition, newMid - rootPosition);
    setModelRotation(skeleton, pose, before, chain.root, rootRotation * before.rotation[chain.root]);
    ModelPose& rotated = scratch.second;
    computeModelPose(skeleton, pose, rotated);
    const glm::vec3 newTip = rootPosition + (targetModel - rootPosition) * (distance / rawDistance);
    const glm::quat midRotation = rotationBetween(rotated.position[chain.tip] - rotated.position[chain.mid], newTip - rotated.position[chain.mid]);
    setModelRotation(skeleton, pose, rotated, chain.mid, midRotation * rotated.rotation[chain.mid]);
    if (weight < 1.0f) {
        pose.joints[chain.root].rotation = nlerp(originalRoot, pose.joints[chain.root].rotation, weight);
        pose.joints[chain.mid].rotation = nlerp(originalMid, pose.joints[chain.mid].rotation, weight);
    }
    return true;
}

void solveLookAt(const Skeleton& skeleton, Pose& pose, const std::vector<LookAtJoint>& chain, const glm::vec3& forwardAxis, const glm::vec3& targetModel, float maxAngleRadians, float weight) {
    if (chain.empty() || !validJoint(skeleton, chain.back().joint) || !poseCovers(skeleton, pose) || !finiteVec(targetModel)) return;
    const int head = chain.back().joint;
    ModelScratch& scratch = lookAtScratch();
    ModelPose& headModel = scratch.first;
    computeModelPose(skeleton, pose, headModel);
    const glm::vec3 forward = headModel.rotation[head] * forwardAxis;
    const glm::vec3 wanted = targetModel - headModel.position[head];
    if (glm::length(wanted) < EPSILON_BONE) return;
    const glm::quat fullRotation = limitAngle(rotationBetween(forward, wanted), maxAngleRadians);
    for (const LookAtJoint& lookJoint : chain) {
        if (!validJoint(skeleton, lookJoint.joint)) continue;
        ModelPose& current = scratch.second;
        computeModelPose(skeleton, pose, current);
        const glm::quat partial = nlerp(IDENTITY_ROTATION, fullRotation, std::clamp(lookJoint.weight * weight, 0.0f, 1.0f));
        setModelRotation(skeleton, pose, current, lookJoint.joint, partial * current.rotation[lookJoint.joint]);
    }
}

namespace {
bool validLeg(const Skeleton& skeleton, const FootIkLeg& leg) { return validJoint(skeleton, leg.chain.root) && validJoint(skeleton, leg.chain.mid) && validJoint(skeleton, leg.chain.tip); }
}

FootIkResult solveFootIk(const Skeleton& skeleton, Pose& pose, const std::vector<FootIkLeg>& legs, const FootIkSettings& settings, const glm::mat4& characterToWorld, const GroundFn& ground) {
    FootIkResult result;
    if (!poseCovers(skeleton, pose)) {
        result.planted.assign(legs.size(), 0);
        result.footError.assign(legs.size(), 0.0f);
        return result;
    }
    result.planted.assign(legs.size(), 0);
    result.footError.assign(legs.size(), 0.0f);
    const glm::mat4 worldToCharacter = glm::inverse(characterToWorld);
    ModelScratch& scratch = footIkScratch();
    ModelPose& model = scratch.first;
    computeModelPose(skeleton, pose, model);
    std::vector<std::optional<GroundSample>> samples(legs.size());
    float drop = 0.0f;
    for (size_t legIndex = 0; legIndex < legs.size(); ++legIndex) {
        if (!validLeg(skeleton, legs[legIndex])) continue;
        const glm::vec3 worldTip = transformPoint(characterToWorld, model.position[legs[legIndex].chain.tip]);
        samples[legIndex] = ground(worldTip);
        if (!samples[legIndex]) continue;
        drop = std::min(drop, samples[legIndex]->height + legs[legIndex].ankleHeight - worldTip.y);
    }
    drop = std::clamp(drop * settings.weight, -settings.maxPelvisDrop, 0.0f);
    if (validJoint(skeleton, settings.pelvis) && drop != 0.0f) {
        const int parent = parentOf(skeleton, static_cast<size_t>(settings.pelvis));
        const glm::quat parentRotation = parent >= 0 ? model.rotation[parent] : IDENTITY_ROTATION;
        pose.joints[settings.pelvis].position = pose.joints[settings.pelvis].position + conjugate(parentRotation) * glm::vec3(0.0f, drop, 0.0f);
        result.pelvisOffset = drop;
    }
    ModelPose& lowered = scratch.second;
    computeModelPose(skeleton, pose, lowered);
    for (size_t legIndex = 0; legIndex < legs.size(); ++legIndex) {
        if (!samples[legIndex]) continue;
        const FootIkLeg& leg = legs[legIndex];
        const glm::vec3 worldTip = transformPoint(characterToWorld, lowered.position[leg.chain.tip]);
        const glm::vec3 targetModel = transformPoint(worldToCharacter, glm::vec3(worldTip.x, samples[legIndex]->height + leg.ankleHeight, worldTip.z));
        const glm::vec3 knee = lowered.position[leg.chain.mid];
        const glm::vec3 pole = knee + lowered.rotation[leg.chain.root] * glm::vec3(0.0f, 0.0f, 1.0f);
        if (solveTwoBone(skeleton, pose, leg.chain, targetModel, pole, settings.weight)) result.planted[legIndex] = 1;
        ModelPose& solved = scratch.third;
        computeModelPose(skeleton, pose, solved);
        result.footError[legIndex] = glm::length(solved.position[leg.chain.tip] - targetModel);
    }
    return result;
}
