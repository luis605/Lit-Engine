module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

module Engine.SkinRig;

import Engine.glm;
import Engine.Skin;
import Engine.Rig;
import Engine.Face;
import Engine.Animation;

namespace {

constexpr int kKeysPerCycle = 9;
constexpr float kTwoPi = 6.28318530718f;

glm::quat axisAngle(const glm::vec3& axis, float radians) { return glm::angleAxis(radians, axis); }

template <typename Fn>
void addRotationTrack(SkeletalClip& clip, const Skeleton& skeleton, const char* joint, float duration, Fn&& angle, const glm::vec3& axis = glm::vec3(1.0f, 0.0f, 0.0f)) {
    const int index = skeleton.find(joint);
    if (index < 0) return;
    JointTrack track;
    track.joint = index;
    for (int k = 0; k <= kKeysPerCycle; ++k) {
        const float phase = static_cast<float>(k) / static_cast<float>(kKeysPerCycle);
        track.rotation.push_back({phase * duration, skeleton.joints[static_cast<size_t>(index)].rotation * axisAngle(axis, angle(phase * kTwoPi))});
    }
    clip.tracks.push_back(std::move(track));
}

}

Skeleton skeletonFromSkin(const DynamicMesh& mesh) {
    Skeleton skeleton;
    for (const SkinJoint& joint : mesh.joints) skeleton.add(joint.name, joint.parent, joint.position, joint.rotation, joint.scale);
    return skeleton;
}

SkeletalClip clipFromSkin(const SkinClip& clip) {
    SkeletalClip out;
    out.name = clip.name;
    out.duration = clip.duration;
    out.loop = true;
    for (const SkinClipChannel& channel : clip.channels) {
        JointTrack track;
        track.joint = channel.joint;
        for (const auto& [time, value] : channel.position) track.position.push_back({time, value});
        for (const auto& [time, value] : channel.rotation) track.rotation.push_back({time, value});
        for (const auto& [time, value] : channel.scale) track.scale.push_back({time, value});
        out.tracks.push_back(std::move(track));
    }
    return out;
}

void computeSkinPalette(const Skeleton& skeleton, const Pose& pose, const DynamicMesh& mesh, std::vector<glm::mat4>& palette) {
    static thread_local std::vector<glm::mat4> model;
    computeModelMatrices(skeleton, pose, model);
    buildBonePalette(model, mesh.joints, palette);
}

void morphWeightsFromBlendshapes(const DynamicMesh& mesh, const BlendshapeWeights& weights, std::vector<float>& out) {
    out.assign(mesh.morphs.size(), 0.0f);
    for (size_t i = 0; i < mesh.morphs.size(); ++i) {
        const int channel = blendshapeIndex(mesh.morphs[i].name);
        if (channel >= 0) out[i] = weights[static_cast<size_t>(channel)];
    }
}

SkeletalClip makeWalkClip(const Skeleton& skeleton, float duration) {
    SkeletalClip clip;
    clip.name = "walk";
    clip.duration = duration;
    clip.loop = true;
    const float swing = 0.55f;
    addRotationTrack(clip, skeleton, "hipL", duration, [=](float p) { return swing * std::sin(p); });
    addRotationTrack(clip, skeleton, "hipR", duration, [=](float p) { return -swing * std::sin(p); });
    addRotationTrack(clip, skeleton, "kneeL", duration, [=](float p) { return 0.5f * (1.0f - std::cos(p + 1.2f)) * 0.55f; });
    addRotationTrack(clip, skeleton, "kneeR", duration, [=](float p) { return 0.5f * (1.0f - std::cos(p + 1.2f + 3.14159f)) * 0.55f; });
    addRotationTrack(clip, skeleton, "shoulderL", duration, [=](float p) { return -0.5f * std::sin(p); });
    addRotationTrack(clip, skeleton, "shoulderR", duration, [=](float p) { return 0.5f * std::sin(p); });
    addRotationTrack(clip, skeleton, "elbowL", duration, [=](float p) { return -0.3f - 0.2f * std::sin(p); });
    addRotationTrack(clip, skeleton, "elbowR", duration, [=](float p) { return -0.3f + 0.2f * std::sin(p); });
    addRotationTrack(clip, skeleton, "spine", duration, [=](float p) { return 0.12f * std::sin(p); }, glm::vec3(0.0f, 1.0f, 0.0f));
    addRotationTrack(clip, skeleton, "chest", duration, [=](float p) { return -0.1f * std::sin(p); }, glm::vec3(0.0f, 1.0f, 0.0f));
    const int hips = skeleton.find("hips");
    if (hips >= 0) {
        JointTrack track;
        track.joint = hips;
        const glm::vec3 bind = skeleton.joints[static_cast<size_t>(hips)].position;
        for (int k = 0; k <= kKeysPerCycle; ++k) {
            const float phase = static_cast<float>(k) / static_cast<float>(kKeysPerCycle);
            track.position.push_back({phase * duration, bind + glm::vec3(0.0f, 0.04f * std::cos(2.0f * phase * kTwoPi), 0.0f)});
        }
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}

SkeletalClip makeWaveClip(const Skeleton& skeleton, float duration) {
    SkeletalClip clip;
    clip.name = "wave";
    clip.duration = duration;
    clip.loop = true;
    addRotationTrack(clip, skeleton, "shoulderR", duration, [](float) { return 2.7f; }, glm::vec3(0.0f, 0.0f, 1.0f));
    addRotationTrack(clip, skeleton, "elbowR", duration, [](float p) { return 0.5f * std::sin(2.0f * p); }, glm::vec3(0.0f, 0.0f, 1.0f));
    addRotationTrack(clip, skeleton, "spine", duration, [](float p) { return 0.06f * std::sin(p); }, glm::vec3(0.0f, 1.0f, 0.0f));
    addRotationTrack(clip, skeleton, "head", duration, [](float p) { return 0.15f * std::sin(p); }, glm::vec3(0.0f, 1.0f, 0.0f));
    return clip;
}

BlendshapeClip makeTalkClip(float duration) {
    BlendshapeClip clip;
    clip.name = "talk";
    clip.duration = duration;
    clip.loop = true;
    auto curve = [&](const char* name, std::initializer_list<FloatKey> keys) {
        BlendshapeCurve c;
        c.channel = blendshapeIndex(name);
        c.keys.assign(keys.begin(), keys.end());
        if (c.channel >= 0) clip.curves.push_back(std::move(c));
    };
    const float d = duration;
    curve("jawOpen", {{0.0f, 0.0f}, {0.1f * d, 0.8f}, {0.2f * d, 0.1f}, {0.35f * d, 0.9f}, {0.5f * d, 0.2f}, {0.65f * d, 0.7f}, {0.8f * d, 0.05f}, {d, 0.0f}});
    curve("mouthSmileLeft", {{0.0f, 0.0f}, {0.5f * d, 0.7f}, {d, 0.0f}});
    curve("mouthSmileRight", {{0.0f, 0.0f}, {0.5f * d, 0.7f}, {d, 0.0f}});
    curve("eyeBlinkLeft", {{0.0f, 0.0f}, {0.7f * d, 0.0f}, {0.74f * d, 1.0f}, {0.78f * d, 0.0f}, {d, 0.0f}});
    curve("eyeBlinkRight", {{0.0f, 0.0f}, {0.7f * d, 0.0f}, {0.74f * d, 1.0f}, {0.78f * d, 0.0f}, {d, 0.0f}});
    curve("browInnerUp", {{0.0f, 0.0f}, {0.3f * d, 0.8f}, {0.6f * d, 0.0f}, {d, 0.0f}});
    curve("cheekPuff", {{0.0f, 0.0f}, {0.9f * d, 0.0f}, {0.95f * d, 0.6f}, {d, 0.0f}});
    return clip;
}
