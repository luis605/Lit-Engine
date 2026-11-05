#include "Review3Suites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <limits>
#include <vector>

import Engine.Rig;
import Engine.glm;

namespace {

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

Skeleton leg() {
    Skeleton skeleton;
    skeleton.add("hip", -1, glm::vec3(0.0f));
    skeleton.add("knee", 0, glm::vec3(0.0f, -1.0f, 0.0f));
    skeleton.add("foot", 1, glm::vec3(0.0f, -1.0f, 0.0f));
    return skeleton;
}

void testBadParentsDoNotEscape() {
    Skeleton skeleton = leg();
    skeleton.joints[1].parent = 99;
    skeleton.joints[2].parent = 2;
    const Pose pose = Pose::bind(skeleton);
    const ModelPose model = computeModelPose(skeleton, pose);
    CHECK(model.position.size() == 3);
    CHECK(computeModelMatrices(skeleton, pose).size() == 3);
    CHECK(subtreeMask(skeleton, 1).size() == 3);
    Skeleton cycle = leg();
    cycle.joints[0].parent = 1;
    CHECK(subtreeMask(cycle, 2).size() == 3);
}

void testShortPoseSolvers() {
    const Skeleton skeleton = leg();
    Pose empty;
    CHECK(!solveTwoBone(skeleton, empty, TwoBoneChain{0, 1, 2}, glm::vec3(0.0f, -1.5f, 0.5f), std::nullopt, 1.0f));
    std::vector<LookAtJoint> chain = {{2, 1.0f}};
    solveLookAt(skeleton, empty, chain, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, 1.0f);
    FootIkResult result = solveFootIk(skeleton, empty, {FootIkLeg{TwoBoneChain{0, 1, 2}, 0.0f}}, FootIkSettings{}, glm::mat4(1.0f), [](const glm::vec3&) { return std::optional<GroundSample>(); });
    CHECK(result.planted.size() == 1);
    CHECK(empty.joints.empty());
}

void testNanTargetKeepsPose() {
    const Skeleton skeleton = leg();
    Pose pose = Pose::bind(skeleton);
    CHECK(!solveTwoBone(skeleton, pose, TwoBoneChain{0, 1, 2}, glm::vec3(kNan, 0.0f, 0.0f), std::nullopt, 1.0f));
    for (const JointPose& joint : pose.joints) CHECK(std::isfinite(joint.rotation.w));
}

void testBlendAliasingAndMismatch() {
    const Skeleton skeleton = leg();
    Pose first = Pose::bind(skeleton);
    Pose second = Pose::bind(skeleton);
    second.joints[1].position = glm::vec3(2.0f, 0.0f, 0.0f);
    blendPoses(first, second, 0.5f, first);
    CHECK(std::fabs(first.joints[1].position.x - 1.0f) < 1.0e-5f);
    Pose shorter;
    shorter.joints.resize(1);
    blendPoses(first, shorter, 0.5f, shorter);
    CHECK(shorter.joints.size() == 3);
    blendPosesMasked(shorter, first, {}, 0.5f, shorter);
    Pose none;
    blendPoses(none, first, 0.5f, none);
    CHECK(none.joints.empty());
}

void testEmptyClipAndNanTime() {
    const Skeleton skeleton = leg();
    SkeletalClip clip;
    Pose pose;
    sampleSkeletalClip(skeleton, clip, kNan, pose);
    CHECK(pose.joints.size() == 3);
    JointTrack track;
    track.joint = 1;
    track.position = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(1.0f, 0.0f, 0.0f)}};
    clip.tracks.push_back(track);
    clip.tracks.push_back(JointTrack{99, {}, {}, {}});
    sampleSkeletalClip(skeleton, clip, kNan, pose);
    CHECK(pose.joints.size() == 3);
}

void testBlendSpaceNanParameter() {
    const Skeleton skeleton = leg();
    SkeletalLibrary library;
    SkeletalClip slow;
    slow.duration = 1.0f;
    SkeletalClip fast;
    fast.duration = 0.5f;
    const uint32_t slowId = library.add(slow);
    const uint32_t fastId = library.add(fast);
    SkeletalAnimator animator(skeleton, library);
    animator.playBlendSpace({{0.0f, slowId}, {1.0f, fastId}}, 0.5f, 0.0f);
    animator.setBlendParameter(kNan);
    animator.update(0.1f);
    for (const JointPose& joint : animator.pose().joints) CHECK(std::isfinite(joint.position.x));
}

}

void runReview3AnimationTests() {
    testBadParentsDoNotEscape();
    testShortPoseSolvers();
    testNanTargetKeepsPose();
    testBlendAliasingAndMismatch();
    testEmptyClipAndNanTime();
    testBlendSpaceNanParameter();
}
