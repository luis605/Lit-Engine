module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

module Engine.Rig;

namespace {
glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float lf = glm::length(from);
    const float lt = glm::length(to);
    if (lf < 1.0e-8f || lt < 1.0e-8f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 a = from / lf;
    const glm::vec3 b = to / lt;
    const glm::vec3 c = glm::cross(a, b);
    const float s = glm::length(c);
    const float d = glm::dot(a, b);
    if (s < 1.0e-7f) {
        if (d > 0.0f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 axis = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::length(axis) < 1.0e-4f) axis = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    return glm::angleAxis(std::atan2(s, d), c / s);
}

glm::quat nlerp(const glm::quat& a, const glm::quat& b, float t) {
    const float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    const glm::quat bb = dot < 0.0f ? glm::quat(-b.w, -b.x, -b.y, -b.z) : b;
    glm::quat r(a.w + (bb.w - a.w) * t, a.x + (bb.x - a.x) * t, a.y + (bb.y - a.y) * t, a.z + (bb.z - a.z) * t);
    const float len = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
    return len > 0.0f ? glm::quat(r.w / len, r.x / len, r.y / len, r.z / len) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::quat conj(const glm::quat& q) { return glm::quat(q.w, -q.x, -q.y, -q.z); }

glm::vec3 lerp3(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }

glm::quat limitAngle(const glm::quat& q, float maxAngle) {
    const float w = std::clamp(q.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(std::fabs(w));
    if (angle <= maxAngle || angle < 1.0e-6f) return q;
    return nlerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), q, maxAngle / angle);
}

template <typename Key, typename Lerp>
auto sampleKeys(const std::vector<Key>& keys, float t, Lerp lerp) {
    const auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const Key& k) { return v < k.time; });
    if (it == keys.end()) return keys.back().value;
    if (it == keys.begin()) return it->value;
    const Key& p = *(it - 1);
    const float span = it->time - p.time;
    return lerp(p.value, it->value, span > 1.0e-8f ? (t - p.time) / span : 1.0f);
}

void setModelRotation(const Skeleton& sk, Pose& pose, const ModelPose& model, int joint, const glm::quat& wanted) {
    const int parent = sk.joints[joint].parent;
    const glm::quat parentRot = parent >= 0 ? model.rotation[parent] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    pose.joints[joint].rotation = nlerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), conj(parentRot) * wanted, 1.0f);
}
}

int Skeleton::add(const std::string& name, int parent, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    joints.push_back({name, parent, position, rotation, scale});
    return static_cast<int>(joints.size() - 1);
}

int Skeleton::find(const std::string& name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

Pose Pose::bind(const Skeleton& skeleton) {
    Pose p;
    p.joints.reserve(skeleton.joints.size());
    for (const Joint& j : skeleton.joints) p.joints.push_back({j.position, j.rotation, j.scale});
    return p;
}

uint32_t SkeletalLibrary::add(SkeletalClip clip) {
    for (JointTrack& t : clip.tracks) {
        const auto byTime = [](const auto& a, const auto& b) { return a.time < b.time; };
        std::stable_sort(t.position.begin(), t.position.end(), byTime);
        std::stable_sort(t.rotation.begin(), t.rotation.end(), byTime);
        std::stable_sort(t.scale.begin(), t.scale.end(), byTime);
        if (clip.duration <= 0.0f) {
            if (!t.position.empty()) clip.duration = std::max(clip.duration, t.position.back().time);
            if (!t.rotation.empty()) clip.duration = std::max(clip.duration, t.rotation.back().time);
            if (!t.scale.empty()) clip.duration = std::max(clip.duration, t.scale.back().time);
        }
    }
    m_clips.push_back(std::move(clip));
    return static_cast<uint32_t>(m_clips.size() - 1);
}

int SkeletalLibrary::find(const std::string& name) const {
    for (size_t i = 0; i < m_clips.size(); ++i) {
        if (m_clips[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

void sampleSkeletalClip(const Skeleton& skeleton, const SkeletalClip& clip, float time, Pose& out) {
    out = Pose::bind(skeleton);
    float t = time;
    if (clip.duration > 0.0f) {
        if (clip.loop) {
            t = std::fmod(t, clip.duration);
            if (t < 0.0f) t += clip.duration;
        } else {
            t = std::clamp(t, 0.0f, clip.duration);
        }
    }
    for (const JointTrack& track : clip.tracks) {
        if (track.joint < 0 || track.joint >= static_cast<int>(out.joints.size())) continue;
        JointPose& jp = out.joints[track.joint];
        if (!track.position.empty()) jp.position = sampleKeys(track.position, t, lerp3);
        if (!track.rotation.empty()) jp.rotation = sampleKeys(track.rotation, t, [](const glm::quat& a, const glm::quat& b, float f) { return nlerp(a, b, f); });
        if (!track.scale.empty()) jp.scale = sampleKeys(track.scale, t, lerp3);
    }
}

void blendPoses(const Pose& a, const Pose& b, float weight, Pose& out) {
    Pose result;
    result.joints.resize(a.joints.size());
    for (size_t i = 0; i < a.joints.size(); ++i) {
        result.joints[i].position = lerp3(a.joints[i].position, b.joints[i].position, weight);
        result.joints[i].rotation = nlerp(a.joints[i].rotation, b.joints[i].rotation, weight);
        result.joints[i].scale = lerp3(a.joints[i].scale, b.joints[i].scale, weight);
    }
    out = std::move(result);
}

void blendPosesMasked(const Pose& a, const Pose& b, const std::vector<float>& mask, float weight, Pose& out) {
    Pose result;
    result.joints.resize(a.joints.size());
    for (size_t i = 0; i < a.joints.size(); ++i) {
        const float w = weight * (i < mask.size() ? mask[i] : 1.0f);
        result.joints[i].position = lerp3(a.joints[i].position, b.joints[i].position, w);
        result.joints[i].rotation = nlerp(a.joints[i].rotation, b.joints[i].rotation, w);
        result.joints[i].scale = lerp3(a.joints[i].scale, b.joints[i].scale, w);
    }
    out = std::move(result);
}

void addPose(Pose& base, const Pose& layer, const Pose& reference, float weight) {
    for (size_t i = 0; i < base.joints.size(); ++i) {
        base.joints[i].position = base.joints[i].position + (layer.joints[i].position - reference.joints[i].position) * weight;
        const glm::quat delta = layer.joints[i].rotation * conj(reference.joints[i].rotation);
        base.joints[i].rotation = nlerp(base.joints[i].rotation, delta * base.joints[i].rotation, weight);
        base.joints[i].scale = base.joints[i].scale + (layer.joints[i].scale - reference.joints[i].scale) * weight;
    }
}

std::vector<float> subtreeMask(const Skeleton& skeleton, int rootJoint, bool includeRoot) {
    std::vector<float> mask(skeleton.joints.size(), 0.0f);
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        for (int j = static_cast<int>(i); j >= 0; j = skeleton.joints[j].parent) {
            if (j == rootJoint) {
                mask[i] = (static_cast<int>(i) == rootJoint && !includeRoot) ? 0.0f : 1.0f;
                break;
            }
        }
    }
    return mask;
}

ModelPose computeModelPose(const Skeleton& skeleton, const Pose& pose) {
    ModelPose m;
    const size_t n = skeleton.joints.size();
    m.position.resize(n);
    m.rotation.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const int p = skeleton.joints[i].parent;
        const JointPose& jp = pose.joints[i];
        if (p < 0) {
            m.position[i] = jp.position;
            m.rotation[i] = jp.rotation;
        } else {
            const glm::vec3 scaled(jp.position.x * pose.joints[p].scale.x, jp.position.y * pose.joints[p].scale.y, jp.position.z * pose.joints[p].scale.z);
            m.position[i] = m.position[p] + m.rotation[p] * scaled;
            m.rotation[i] = m.rotation[p] * jp.rotation;
        }
    }
    return m;
}

std::vector<glm::mat4> computeModelMatrices(const Skeleton& skeleton, const Pose& pose) {
    std::vector<glm::mat4> out(skeleton.joints.size());
    for (size_t i = 0; i < out.size(); ++i) {
        const JointPose& jp = pose.joints[i];
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), jp.position) * glm::mat4_cast(jp.rotation) * glm::scale(glm::mat4(1.0f), jp.scale);
        const int p = skeleton.joints[i].parent;
        out[i] = p < 0 ? local : out[p] * local;
    }
    return out;
}

void applyPoseToEntities(World& world, const std::vector<EntityHandle>& entities, const Pose& pose) {
    for (size_t i = 0; i < entities.size() && i < pose.joints.size(); ++i) {
        const JointPose& jp = pose.joints[i];
        world.setLocalMatrix(entities[i], glm::translate(glm::mat4(1.0f), jp.position) * glm::mat4_cast(jp.rotation) * glm::scale(glm::mat4(1.0f), jp.scale));
    }
}

void SkeletalAnimator::play(uint32_t clip, float fadeSeconds) {
    if (m_current.valid && m_current.space.empty() && m_current.clip == clip) return;
    m_previous = m_current;
    m_current = Source{true, clip, {}, 0.0f};
    m_fadeTotal = m_fadeLeft = m_previous.valid ? fadeSeconds : 0.0f;
}

void SkeletalAnimator::playBlendSpace(std::vector<BlendSample> samples, float parameter, float fadeSeconds) {
    std::sort(samples.begin(), samples.end(), [](const BlendSample& a, const BlendSample& b) { return a.x < b.x; });
    m_previous = m_current;
    m_current = Source{true, 0, std::move(samples), 0.0f};
    m_parameter = parameter;
    m_fadeTotal = m_fadeLeft = m_previous.valid ? fadeSeconds : 0.0f;
}

void SkeletalAnimator::setOverlay(uint32_t clip, std::vector<float> mask, float weight, bool additive) {
    if (!m_overlayActive || m_overlay.clip != clip) m_overlay = Source{true, clip, {}, 0.0f};
    m_overlayActive = true;
    m_overlayMask = std::move(mask);
    m_overlayWeight = weight;
    m_overlayAdditive = additive;
}

void SkeletalAnimator::advance(Source& src, float dt) const {
    src.time += dt * m_speed;
}

void SkeletalAnimator::evaluate(const Source& src, Pose& out) const {
    if (!src.valid) {
        out = Pose::bind(*m_skeleton);
        return;
    }
    if (src.space.empty()) {
        const SkeletalClip* c = m_library->get(src.clip);
        if (c) sampleSkeletalClip(*m_skeleton, *c, src.time, out);
        else out = Pose::bind(*m_skeleton);
        return;
    }
    const auto& s = src.space;
    const float x = std::clamp(m_parameter, s.front().x, s.back().x);
    size_t hi = 1;
    while (hi < s.size() && s[hi].x < x) ++hi;
    if (s.size() == 1 || hi >= s.size()) {
        const SkeletalClip* c = m_library->get(s.back().clip);
        if (c) sampleSkeletalClip(*m_skeleton, *c, src.time, out);
        return;
    }
    const BlendSample& a = s[hi - 1];
    const BlendSample& b = s[hi];
    const SkeletalClip* ca = m_library->get(a.clip);
    const SkeletalClip* cb = m_library->get(b.clip);
    if (!ca || !cb) {
        out = Pose::bind(*m_skeleton);
        return;
    }
    const float f = b.x > a.x ? (x - a.x) / (b.x - a.x) : 0.0f;
    const float da = ca->duration > 0.0f ? ca->duration : 1.0f;
    const float db = cb->duration > 0.0f ? cb->duration : 1.0f;
    const float phase = std::fmod(src.time / (da + (db - da) * f), 1.0f);
    Pose pa;
    Pose pb;
    sampleSkeletalClip(*m_skeleton, *ca, phase * da, pa);
    sampleSkeletalClip(*m_skeleton, *cb, phase * db, pb);
    blendPoses(pa, pb, f, out);
}

void SkeletalAnimator::update(float dt) {
    advance(m_current, dt);
    if (m_previous.valid) advance(m_previous, dt);
    if (m_overlayActive) advance(m_overlay, dt);
    Pose cur;
    evaluate(m_current, cur);
    if (m_fadeLeft > 0.0f && m_previous.valid) {
        m_fadeLeft = std::max(0.0f, m_fadeLeft - dt);
        Pose prev;
        evaluate(m_previous, prev);
        const float t = m_fadeTotal > 0.0f ? 1.0f - m_fadeLeft / m_fadeTotal : 1.0f;
        blendPoses(prev, cur, t * t * (3.0f - 2.0f * t), cur);
        if (m_fadeLeft == 0.0f) m_previous.valid = false;
    }
    if (m_overlayActive) {
        Pose over;
        evaluate(m_overlay, over);
        if (m_overlayAdditive) {
            Pose ref = Pose::bind(*m_skeleton);
            std::vector<float> w = m_overlayMask;
            for (size_t i = 0; i < cur.joints.size(); ++i) {
                const float mw = i < w.size() ? w[i] : 1.0f;
                if (mw > 0.0f) {
                    Pose one = cur;
                    addPose(one, over, ref, m_overlayWeight * mw);
                    cur.joints[i] = one.joints[i];
                }
            }
        } else {
            blendPosesMasked(cur, over, m_overlayMask, m_overlayWeight, cur);
        }
    }
    m_pose = std::move(cur);
}

bool solveTwoBone(const Skeleton& sk, Pose& pose, const TwoBoneChain& c, const glm::vec3& target, const std::optional<glm::vec3>& pole, float weight) {
    if (c.root < 0 || c.mid < 0 || c.tip < 0) return false;
    const ModelPose before = computeModelPose(sk, pose);
    const glm::vec3 rootP = before.position[c.root];
    const glm::vec3 midP = before.position[c.mid];
    const glm::vec3 tipP = before.position[c.tip];
    const float a = glm::length(midP - rootP);
    const float b = glm::length(tipP - midP);
    if (a < 1.0e-6f || b < 1.0e-6f) return false;
    const glm::vec3 toTarget = target - rootP;
    const float rawD = glm::length(toTarget);
    if (rawD < 1.0e-6f) return false;
    const float d = std::clamp(rawD, std::fabs(a - b) + 1.0e-4f, a + b - 1.0e-4f);
    const glm::vec3 dir = toTarget / rawD;
    glm::vec3 hint = pole ? (*pole - rootP) : (midP - rootP);
    hint = hint - dir * glm::dot(hint, dir);
    if (glm::length(hint) < 1.0e-5f) {
        hint = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::length(hint) < 1.0e-5f) hint = glm::cross(dir, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    hint = glm::normalize(hint);
    const float cosA = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    const glm::vec3 newMid = rootP + dir * (a * cosA) + hint * (a * sinA);

    const Pose original = pose;
    const glm::quat q1 = rotationBetween(midP - rootP, newMid - rootP);
    setModelRotation(sk, pose, before, c.root, q1 * before.rotation[c.root]);
    ModelPose mid = computeModelPose(sk, pose);
    const glm::vec3 newTip = rootP + (target - rootP) * (d / rawD);
    const glm::quat q2 = rotationBetween(mid.position[c.tip] - mid.position[c.mid], newTip - mid.position[c.mid]);
    setModelRotation(sk, pose, mid, c.mid, q2 * mid.rotation[c.mid]);
    if (weight < 1.0f) {
        pose.joints[c.root].rotation = nlerp(original.joints[c.root].rotation, pose.joints[c.root].rotation, weight);
        pose.joints[c.mid].rotation = nlerp(original.joints[c.mid].rotation, pose.joints[c.mid].rotation, weight);
    }
    return true;
}

void solveLookAt(const Skeleton& sk, Pose& pose, const std::vector<LookAtJoint>& chain, const glm::vec3& axis, const glm::vec3& target, float maxAngle, float weight) {
    if (chain.empty()) return;
    const int head = chain.back().joint;
    if (head < 0) return;
    const ModelPose m = computeModelPose(sk, pose);
    const glm::vec3 fwd = m.rotation[head] * axis;
    const glm::vec3 want = target - m.position[head];
    if (glm::length(want) < 1.0e-6f) return;
    const glm::quat full = limitAngle(rotationBetween(fwd, want), maxAngle);
    for (const LookAtJoint& lj : chain) {
        if (lj.joint < 0) continue;
        const ModelPose cur = computeModelPose(sk, pose);
        const glm::quat part = nlerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), full, std::clamp(lj.weight * weight, 0.0f, 1.0f));
        setModelRotation(sk, pose, cur, lj.joint, part * cur.rotation[lj.joint]);
    }
}

FootIkResult solveFootIk(const Skeleton& sk, Pose& pose, const std::vector<FootIkLeg>& legs, const FootIkSettings& settings, const glm::mat4& toWorld, const GroundFn& ground) {
    FootIkResult result;
    result.planted.assign(legs.size(), 0);
    result.footError.assign(legs.size(), 0.0f);
    const glm::mat4 toModel = glm::inverse(toWorld);
    const ModelPose m = computeModelPose(sk, pose);
    std::vector<std::optional<GroundSample>> samples(legs.size());
    std::vector<float> delta(legs.size(), 0.0f);
    float drop = 0.0f;
    for (size_t i = 0; i < legs.size(); ++i) {
        const glm::vec3 tip = m.position[legs[i].chain.tip];
        const glm::vec4 w = toWorld * glm::vec4(tip.x, tip.y, tip.z, 1.0f);
        samples[i] = ground(glm::vec3(w.x, w.y, w.z));
        if (!samples[i]) continue;
        delta[i] = samples[i]->height + legs[i].ankleHeight - w.y;
        drop = std::min(drop, delta[i]);
    }
    drop = std::clamp(drop * settings.weight, -settings.maxPelvisDrop, 0.0f);
    if (settings.pelvis >= 0 && drop != 0.0f) {
        const int parent = sk.joints[settings.pelvis].parent;
        const glm::quat pr = parent >= 0 ? m.rotation[parent] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        pose.joints[settings.pelvis].position = pose.joints[settings.pelvis].position + conj(pr) * glm::vec3(0.0f, drop, 0.0f);
        result.pelvisOffset = drop;
    }
    const ModelPose lowered = computeModelPose(sk, pose);
    for (size_t i = 0; i < legs.size(); ++i) {
        if (!samples[i]) continue;
        const glm::vec3 tip = lowered.position[legs[i].chain.tip];
        const glm::vec4 w = toWorld * glm::vec4(tip.x, tip.y, tip.z, 1.0f);
        const glm::vec4 tm = toModel * glm::vec4(w.x, samples[i]->height + legs[i].ankleHeight, w.z, 1.0f);
        const glm::vec3 targetModel(tm.x, tm.y, tm.z);
        const glm::vec3 knee = lowered.position[legs[i].chain.mid];
        glm::vec3 pole = knee + lowered.rotation[legs[i].chain.root] * glm::vec3(0.0f, 0.0f, 1.0f);
        if (solveTwoBone(sk, pose, legs[i].chain, targetModel, pole, settings.weight)) result.planted[i] = 1;
        const ModelPose done = computeModelPose(sk, pose);
        const glm::vec3 reached = done.position[legs[i].chain.tip];
        result.footError[i] = glm::length(reached - targetModel);
    }
    return result;
}
