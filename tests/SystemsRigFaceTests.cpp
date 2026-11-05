#include "TestSupport.hpp"
#include "SystemsSuites.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

import Engine.AI;
import Engine.Nav;
import Engine.Rig;
import Engine.Face;
import Engine.Audio;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Params;
import Engine.glm;
import Engine.Json;
import Engine.Rng;

static bool nearlyEqual(float a, float b, float eps = 1.0e-3f) { return std::fabs(a - b) <= eps; }

static Skeleton legSkeleton() {
    Skeleton sk;
    sk.add("pelvis", -1, glm::vec3(0.0f, 1.0f, 0.0f));
    const int tl = sk.add("thighL", 0, glm::vec3(0.1f, 0.0f, 0.0f));
    const int sl = sk.add("shinL", tl, glm::vec3(0.0f, -0.5f, 0.0f));
    sk.add("footL", sl, glm::vec3(0.0f, -0.5f, 0.0f));
    const int tr = sk.add("thighR", 0, glm::vec3(-0.1f, 0.0f, 0.0f));
    const int sr = sk.add("shinR", tr, glm::vec3(0.0f, -0.5f, 0.0f));
    sk.add("footR", sr, glm::vec3(0.0f, -0.5f, 0.0f));
    const int spine = sk.add("spine", 0, glm::vec3(0.0f, 0.5f, 0.0f));
    const int neck = sk.add("neck", spine, glm::vec3(0.0f, 0.3f, 0.0f));
    sk.add("head", neck, glm::vec3(0.0f, 0.1f, 0.0f));
    return sk;
}

static void testRig() {
    const Skeleton sk = legSkeleton();
    CHECK(sk.find("footR") == 6 && sk.find("nope") == -1);
    Pose bind = Pose::bind(sk);
    const ModelPose mp = computeModelPose(sk, bind);
    CHECK(nearlyEqual(mp.position[3].y, 0.0f) && nearlyEqual(mp.position[3].x, 0.1f));

    Pose pose = bind;
    const TwoBoneChain left{1, 2, 3};
    const glm::vec3 target(0.1f, 0.2f, 0.3f);
    CHECK(solveTwoBone(sk, pose, left, target, glm::vec3(0.1f, 0.5f, 1.0f)));
    ModelPose after = computeModelPose(sk, pose);
    CHECK(glm::length(after.position[3] - target) < 1.0e-3f);
    CHECK(nearlyEqual(glm::length(after.position[2] - after.position[1]), 0.5f, 1.0e-3f));
    CHECK(after.position[2].z > 0.1f);

    Pose far = bind;
    solveTwoBone(sk, far, left, glm::vec3(0.1f, -3.0f, 0.0f));
    after = computeModelPose(sk, far);
    CHECK(nearlyEqual(glm::length(after.position[3] - after.position[1]), 1.0f, 2.0e-3f));

    Pose none = bind;
    solveTwoBone(sk, none, left, target, std::nullopt, 0.0f);
    const ModelPose same = computeModelPose(sk, none);
    CHECK(glm::length(same.position[3] - mp.position[3]) < 1.0e-4f);

    Pose stand = bind;
    const std::vector<FootIkLeg> legs{{TwoBoneChain{1, 2, 3}, 0.0f}, {TwoBoneChain{4, 5, 6}, 0.0f}};
    FootIkSettings settings;
    settings.pelvis = 0;
    const FootIkResult res = solveFootIk(sk, stand, legs, settings, glm::mat4(1.0f), [](const glm::vec3& p) -> std::optional<GroundSample> {
        GroundSample g;
        g.height = p.x > 0.0f ? -0.2f : 0.0f;
        return g;
    });
    CHECK(nearlyEqual(res.pelvisOffset, -0.2f, 1.0e-3f));
    CHECK(res.planted[0] && res.planted[1]);
    CHECK(res.footError[0] < 2.0e-3f && res.footError[1] < 2.0e-3f);
    const ModelPose planted = computeModelPose(sk, stand);
    CHECK(nearlyEqual(planted.position[3].y, -0.2f, 2.0e-3f) && nearlyEqual(planted.position[6].y, 0.0f, 2.0e-3f));

    Pose gaze = bind;
    const std::vector<LookAtJoint> chain{{7, 0.2f}, {8, 0.3f}, {9, 0.5f}};
    const glm::vec3 lookTarget(3.0f, 1.9f, 3.0f);
    solveLookAt(sk, gaze, chain, glm::vec3(0.0f, 0.0f, 1.0f), lookTarget);
    const ModelPose gp = computeModelPose(sk, gaze);
    const glm::vec3 fwd = gp.rotation[9] * glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 want = glm::normalize(lookTarget - gp.position[9]);
    CHECK(glm::dot(fwd, want) > 0.9995f);
    Pose limited = bind;
    solveLookAt(sk, limited, chain, glm::vec3(0.0f, 0.0f, 1.0f), lookTarget, glm::radians(20.0f));
    const ModelPose lp = computeModelPose(sk, limited);
    const glm::vec3 lf = lp.rotation[9] * glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK(nearlyEqual(std::acos(std::clamp(glm::dot(lf, glm::vec3(0.0f, 0.0f, 1.0f)), -1.0f, 1.0f)), glm::radians(20.0f), 0.01f));

    SkeletalLibrary lib;
    SkeletalClip a;
    a.name = "low";
    a.tracks.push_back({0, {{0.0f, glm::vec3(0.0f, 1.0f, 0.0f)}, {1.0f, glm::vec3(0.0f, 1.0f, 0.0f)}}, {}, {}});
    SkeletalClip b;
    b.name = "high";
    b.tracks.push_back({0, {{0.0f, glm::vec3(0.0f, 3.0f, 0.0f)}, {1.0f, glm::vec3(0.0f, 3.0f, 0.0f)}}, {}, {}});
    const uint32_t ia = lib.add(a);
    const uint32_t ib = lib.add(b);
    CHECK(lib.find("high") == static_cast<int>(ib));
    SkeletalAnimator anim(sk, lib);
    anim.play(ia, 0.0f);
    anim.update(0.1f);
    CHECK(nearlyEqual(anim.pose().joints[0].position.y, 1.0f));
    anim.play(ib, 1.0f);
    anim.update(0.5f);
    CHECK(nearlyEqual(anim.pose().joints[0].position.y, 2.0f, 1.0e-3f));
    anim.update(0.6f);
    CHECK(nearlyEqual(anim.pose().joints[0].position.y, 3.0f));

    const std::vector<float> mask = subtreeMask(sk, 1);
    CHECK(mask[1] == 1.0f && mask[2] == 1.0f && mask[3] == 1.0f && mask[0] == 0.0f && mask[4] == 0.0f);
    Pose pa = Pose::bind(sk);
    Pose pb = pa;
    pb.joints[0].position.y = 5.0f;
    pb.joints[2].position.y = -0.1f;
    Pose masked;
    blendPosesMasked(pa, pb, mask, 1.0f, masked);
    CHECK(nearlyEqual(masked.joints[0].position.y, 1.0f) && nearlyEqual(masked.joints[2].position.y, -0.1f));
    Pose half;
    blendPoses(pa, pb, 0.5f, half);
    CHECK(nearlyEqual(half.joints[0].position.y, 3.0f));

    SkeletalClip walkA;
    walkA.name = "slow";
    walkA.duration = 2.0f;
    walkA.tracks.push_back({0, {{0.0f, glm::vec3(0.0f, 1.0f, 0.0f)}, {2.0f, glm::vec3(0.0f, 1.0f, 0.0f)}}, {}, {}});
    SkeletalClip walkB = walkA;
    walkB.name = "fast";
    walkB.duration = 1.0f;
    walkB.tracks[0].position = {{0.0f, glm::vec3(0.0f, 2.0f, 0.0f)}, {1.0f, glm::vec3(0.0f, 2.0f, 0.0f)}};
    const uint32_t ws = lib.add(walkA);
    const uint32_t wf = lib.add(walkB);
    SkeletalAnimator blend(sk, lib);
    blend.playBlendSpace({{0.0f, ws}, {1.0f, wf}}, 0.5f, 0.0f);
    blend.update(0.1f);
    CHECK(nearlyEqual(blend.pose().joints[0].position.y, 1.5f, 1.0e-3f));

    World world;
    std::vector<EntityHandle> joints;
    for (size_t i = 0; i < sk.size(); ++i) joints.push_back(world.create("j" + std::to_string(i)));
    applyPoseToEntities(world, joints, planted.position.empty() ? Pose::bind(sk) : stand);
    CHECK(nearlyEqual(world.getPosition(joints[0]).y, 0.8f, 1.0e-3f));
}

static void testFace() {
    CHECK(kArkitBlendshapes.size() == 52);
    CHECK(blendshapeIndex("eyeBlinkLeft") == 0 && blendshapeIndex("tongueOut") == 51 && blendshapeIndex("jawOpen") == 17 && blendshapeIndex("bogus") == -1);
    std::vector<std::string> names;
    for (const char* n : kArkitBlendshapes) names.emplace_back(n);
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());

    BlendshapeClip clip;
    std::string error;
    CHECK(clip.loadText(R"({"name":"talk","curves":{"jawOpen":[[0,0],[1,1]],"eyeBlinkLeft":[[0,0],[0.5,1],[1,0]]}})", &error));
    BlendshapeWeights w = blendshapeZero();
    clip.sample(0.5f, w);
    CHECK(nearlyEqual(w[blendshapeIndex("jawOpen")], 0.5f) && nearlyEqual(w[blendshapeIndex("eyeBlinkLeft")], 1.0f));
    clip.sample(5.0f, w);
    CHECK(nearlyEqual(w[blendshapeIndex("jawOpen")], 1.0f));
    BlendshapeClip bad;
    CHECK(!bad.loadText(R"({"curves":{"notAShape":[[0,0]]}})", &error));
    BlendshapeClip fps;
    CHECK(fps.loadText(R"({"name":"f","fps":10,"curves":{"jawOpen":[0,0.5,1]}})"));
    CHECK(nearlyEqual(fps.duration, 0.2f));
    BlendshapeClip round;
    CHECK(round.loadJson(clip.toJson()) && round.curves.size() == clip.curves.size() && nearlyEqual(round.duration, clip.duration));

    BlendshapeLibrary lib;
    const uint32_t id = lib.add(clip);
    BlendshapePlayer player;
    player.play(id);
    BlendshapeWeights out = blendshapeZero();
    player.update(0.5f, lib, out);
    CHECK(player.playing() && nearlyEqual(out[blendshapeIndex("jawOpen")], 0.5f));
    player.update(0.6f, lib, out);
    CHECK(!player.playing() && nearlyEqual(out[blendshapeIndex("jawOpen")], 1.0f));

    EyeSystem eyes;
    Pcg32 rng(5);
    eyes.setHead(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    eyes.setTarget(glm::vec3(10.0f, 0.0f, 10.0f));
    for (int i = 0; i < 30; ++i) eyes.update(1.0f / 60.0f, rng);
    BlendshapeWeights ew = blendshapeZero();
    eyes.applyTo(ew);
    CHECK(ew[blendshapeIndex("eyeLookOutLeft")] > 0.85f && ew[blendshapeIndex("eyeLookInRight")] > 0.85f);
    CHECK(ew[blendshapeIndex("eyeLookInLeft")] < 0.05f && ew[blendshapeIndex("eyeLookUpLeft")] < 0.2f);

    const auto simulate = [](uint64_t seed, float* maxBlink, uint32_t* saccades) {
        EyeSystem e;
        Pcg32 r(seed);
        e.setHead(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        e.setTarget(glm::vec3(0.0f, 0.0f, 5.0f));
        float mx = 0.0f;
        float yawSum = 0.0f;
        for (int i = 0; i < 3600; ++i) {
            e.update(1.0f / 60.0f, r);
            mx = std::max(mx, e.blink());
            yawSum += e.yawDegrees();
        }
        *maxBlink = mx;
        *saccades = e.saccadeCount();
        return e.blinkCount() * 1000.0f + yawSum;
    };
    float m1;
    float m2;
    uint32_t s1;
    uint32_t s2;
    const float r1 = simulate(3, &m1, &s1);
    const float r2 = simulate(3, &m2, &s2);
    CHECK(r1 == r2 && s1 == s2);
    CHECK(m1 > 0.9f && s1 > 15);
    const uint32_t blinks = static_cast<uint32_t>(r1 / 1000.0f);
    CHECK(blinks >= 6 && blinks <= 40);
    float m3;
    uint32_t s3;
    CHECK(simulate(4, &m3, &s3) != r1);
}

void runRigFaceTests() {
    testRig();
    testFace();
}
