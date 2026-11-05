#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Harness.hpp"

import Engine.Animation;
import Engine.Rig;
import Engine.Face;
import Engine.glm;
import Engine.Rng;

namespace bench {

namespace {

constexpr size_t kRigCount = 500;
constexpr int kJointCount = 24;
constexpr int kKeysPerTrack = 16;
constexpr float kClipSeconds = 1.0f;
constexpr float kStepSeconds = 1.0f / 60.0f;
constexpr float kFadeSeconds = 0.2f;
constexpr size_t kFaceCount = 500;
constexpr float kHalf = 0.5f;
constexpr float kTwoPi = 6.2831853f;

struct RigFixture {
    Skeleton skeleton;
    SkeletalLibrary library;
    std::vector<SkeletalAnimator> animators;
};

SkeletalClip makeClip(const char* name, float phase) {
    SkeletalClip clip;
    clip.name = name;
    clip.duration = kClipSeconds;
    for (int joint = 1; joint < kJointCount; ++joint) {
        JointTrack track;
        track.joint = joint;
        for (int key = 0; key <= kKeysPerTrack; ++key) {
            const float time = kClipSeconds * static_cast<float>(key) / static_cast<float>(kKeysPerTrack);
            const float angle = 0.5f * std::sin(kTwoPi * time + phase + static_cast<float>(joint));
            track.rotation.push_back({time, glm::angleAxis(angle, glm::vec3(1.0f, 0.0f, 0.0f))});
            track.position.push_back({time, glm::vec3(0.0f, 0.1f * static_cast<float>(joint) + 0.01f * angle, 0.0f)});
        }
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}

std::shared_ptr<RigFixture> buildRigs() {
    auto fixture = std::make_shared<RigFixture>();
    fixture->skeleton.add("root", -1, glm::vec3(0.0f));
    for (int joint = 1; joint < kJointCount; ++joint) fixture->skeleton.add("j" + std::to_string(joint), joint - 1, glm::vec3(0.0f, 0.1f, 0.0f));
    const uint32_t clipA = fixture->library.add(makeClip("a", 0.0f));
    const uint32_t clipB = fixture->library.add(makeClip("b", 1.0f));
    fixture->animators.reserve(kRigCount);
    for (size_t index = 0; index < kRigCount; ++index) {
        fixture->animators.emplace_back(fixture->skeleton, fixture->library);
        fixture->animators.back().play(clipA, kFadeSeconds);
        fixture->animators.back().update(kStepSeconds * static_cast<float>(index % 7));
        fixture->animators.back().play(clipB, kFadeSeconds);
    }
    return fixture;
}

Case rigCase(bool crossfading) {
    auto fixture = buildRigs();
    Case benchCase;
    benchCase.itemsPerIteration = kRigCount;
    benchCase.iterations = 40;
    benchCase.warmup = 5;
    benchCase.run = [fixture, crossfading] {
        if (!crossfading) {
            for (auto& animator : fixture->animators) animator.update(kStepSeconds);
        } else {
            uint32_t clip = 0;
            for (auto& animator : fixture->animators) {
                animator.play(clip++ % 2, kFadeSeconds);
                animator.update(kStepSeconds);
            }
        }
        sink(static_cast<double>(fixture->animators.front().pose().joints[1].position.y));
    };
    return benchCase;
}

Case blendCase() {
    auto fixture = buildRigs();
    auto posesA = std::make_shared<std::vector<Pose>>(kRigCount, Pose::bind(fixture->skeleton));
    auto posesB = std::make_shared<std::vector<Pose>>(kRigCount, Pose::bind(fixture->skeleton));
    auto result = std::make_shared<std::vector<Pose>>(kRigCount, Pose::bind(fixture->skeleton));
    auto model = std::make_shared<ModelPose>();
    Case benchCase;
    benchCase.itemsPerIteration = kRigCount;
    benchCase.iterations = 100;
    benchCase.warmup = 5;
    benchCase.run = [fixture, posesA, posesB, result, model] {
        for (size_t index = 0; index < kRigCount; ++index) {
            blendPoses((*posesA)[index], (*posesB)[index], kHalf, (*result)[index]);
            computeModelPose(fixture->skeleton, (*result)[index], *model);
            sink(static_cast<double>(model->position.back().y));
        }
    };
    return benchCase;
}

struct FaceFixture {
    BlendshapeLibrary library;
    std::vector<EyeSystem> eyes;
    std::vector<BlendshapePlayer> players;
    std::vector<BlendshapeWeights> weights;
    Pcg32 rng{kSeed + 31};
};

Case faceCase() {
    auto fixture = std::make_shared<FaceFixture>();
    BlendshapeClip clip;
    clip.name = "talk";
    clip.loop = true;
    clip.duration = kClipSeconds;
    for (int channel = 0; channel < 12; ++channel) {
        BlendshapeCurve curve;
        curve.channel = channel;
        for (int key = 0; key <= kKeysPerTrack; ++key) curve.keys.push_back({kClipSeconds * static_cast<float>(key) / kKeysPerTrack, key % 2 ? 1.0f : 0.0f});
        clip.curves.push_back(std::move(curve));
    }
    const uint32_t clipId = fixture->library.add(std::move(clip));
    fixture->eyes.resize(kFaceCount);
    fixture->players.resize(kFaceCount);
    fixture->weights.assign(kFaceCount, blendshapeZero());
    for (size_t index = 0; index < kFaceCount; ++index) {
        fixture->eyes[index].setTarget(glm::vec3(static_cast<float>(index % 9), 1.5f, 5.0f));
        fixture->players[index].play(clipId);
    }
    Case benchCase;
    benchCase.itemsPerIteration = kFaceCount;
    benchCase.iterations = 100;
    benchCase.warmup = 5;
    benchCase.run = [fixture] {
        for (size_t index = 0; index < kFaceCount; ++index) {
            fixture->players[index].update(kStepSeconds, fixture->library, fixture->weights[index]);
            fixture->eyes[index].update(kStepSeconds, fixture->rng);
            fixture->eyes[index].applyTo(fixture->weights[index]);
        }
        sink(static_cast<double>(fixture->weights.front()[0]));
    };
    return benchCase;
}

}

void registerAnimationBenches() {
    add("anim.skeletal_update_500rigs", [] { return rigCase(false); });
    add("anim.skeletal_crossfade_500rigs", [] { return rigCase(true); });
    add("anim.blend_modelpose_500rigs", blendCase);
    add("anim.face_eye_update_500", faceCase);
}

}
