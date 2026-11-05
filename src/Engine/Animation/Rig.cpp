module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

module Engine.Rig;

namespace {
constexpr float EPSILON_LENGTH = 1.0e-8f;
constexpr float EPSILON_CROSS = 1.0e-7f;
constexpr float EPSILON_AXIS = 1.0e-4f;
constexpr float EPSILON_BONE = 1.0e-6f;
constexpr float PI = 3.14159265f;
constexpr float DEFAULT_BLEND_DURATION = 1.0f;

const glm::quat IDENTITY_ROTATION(1.0f, 0.0f, 0.0f, 0.0f);

}

namespace RigDetail {

glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to) {
    const float fromLength = glm::length(from);
    const float toLength = glm::length(to);
    if (fromLength < EPSILON_LENGTH || toLength < EPSILON_LENGTH) return IDENTITY_ROTATION;
    const glm::vec3 fromUnit = from / fromLength;
    const glm::vec3 toUnit = to / toLength;
    const glm::vec3 axisCross = glm::cross(fromUnit, toUnit);
    const float sine = glm::length(axisCross);
    const float cosine = glm::dot(fromUnit, toUnit);
    if (sine < EPSILON_CROSS) {
        if (cosine > 0.0f) return IDENTITY_ROTATION;
        glm::vec3 axis = glm::cross(fromUnit, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::length(axis) < EPSILON_AXIS) axis = glm::cross(fromUnit, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::angleAxis(PI, glm::normalize(axis));
    }
    return glm::angleAxis(std::atan2(sine, cosine), axisCross / sine);
}

glm::quat nlerp(const glm::quat& from, const glm::quat& to, float fraction) {
    const float dot = from.w * to.w + from.x * to.x + from.y * to.y + from.z * to.z;
    const glm::quat target = dot < 0.0f ? glm::quat(-to.w, -to.x, -to.y, -to.z) : to;
    const glm::quat blended(from.w + (target.w - from.w) * fraction, from.x + (target.x - from.x) * fraction, from.y + (target.y - from.y) * fraction,
                            from.z + (target.z - from.z) * fraction);
    const float length = std::sqrt(blended.w * blended.w + blended.x * blended.x + blended.y * blended.y + blended.z * blended.z);
    return length > 0.0f ? glm::quat(blended.w / length, blended.x / length, blended.y / length, blended.z / length) : IDENTITY_ROTATION;
}

glm::quat conjugate(const glm::quat& rotation) { return glm::quat(rotation.w, -rotation.x, -rotation.y, -rotation.z); }

glm::quat limitAngle(const glm::quat& rotation, float maxAngle) {
    const float cosineHalf = std::clamp(rotation.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(std::fabs(cosineHalf));
    if (angle <= maxAngle || angle < EPSILON_BONE) return rotation;
    return nlerp(IDENTITY_ROTATION, rotation, maxAngle / angle);
}

bool validJoint(const Skeleton& skeleton, int joint) { return joint >= 0 && joint < static_cast<int>(skeleton.joints.size()); }

int parentOf(const Skeleton& skeleton, size_t jointIndex) {
    const int parent = skeleton.joints[jointIndex].parent;
    return parent >= 0 && static_cast<size_t>(parent) < jointIndex ? parent : -1;
}

bool poseCovers(const Skeleton& skeleton, const Pose& pose) { return pose.joints.size() >= skeleton.joints.size(); }

bool finiteVec(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

void setModelRotation(const Skeleton& skeleton, Pose& pose, const ModelPose& model, int joint, const glm::quat& wanted) {
    const int parent = parentOf(skeleton, static_cast<size_t>(joint));
    const glm::quat parentRotation = parent >= 0 ? model.rotation[parent] : IDENTITY_ROTATION;
    pose.joints[joint].rotation = nlerp(IDENTITY_ROTATION, conjugate(parentRotation) * wanted, 1.0f);
}

glm::vec3 transformPoint(const glm::mat4& matrix, const glm::vec3& point) {
    const glm::vec4 result = matrix * glm::vec4(point.x, point.y, point.z, 1.0f);
    return glm::vec3(result.x, result.y, result.z);
}

}

using namespace RigDetail;

namespace {




glm::vec3 lerpVec3(const glm::vec3& from, const glm::vec3& to, float fraction) { return from + (to - from) * fraction; }


template <typename Key, typename Lerp>
auto sampleKeys(const std::vector<Key>& keys, float time, Lerp lerp) {
    const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float value, const Key& key) { return value < key.time; });
    if (next == keys.end()) return keys.back().value;
    if (next == keys.begin()) return next->value;
    const Key& previous = *(next - 1);
    const float span = next->time - previous.time;
    return lerp(previous.value, next->value, span > EPSILON_LENGTH ? (time - previous.time) / span : 1.0f);
}






glm::mat4 localMatrix(const JointPose& joint) {
    return glm::translate(glm::mat4(1.0f), joint.position) * glm::mat4_cast(joint.rotation) * glm::scale(glm::mat4(1.0f), joint.scale);
}

void blendJoint(const JointPose& from, const JointPose& to, float weight, JointPose& out) {
    out.position = lerpVec3(from.position, to.position, weight);
    out.rotation = nlerp(from.rotation, to.rotation, weight);
    out.scale = lerpVec3(from.scale, to.scale, weight);
}

void addJoint(JointPose& base, const JointPose& layer, const JointPose& reference, float weight) {
    base.position = base.position + (layer.position - reference.position) * weight;
    const glm::quat delta = layer.rotation * conjugate(reference.rotation);
    base.rotation = nlerp(base.rotation, delta * base.rotation, weight);
    base.scale = base.scale + (layer.scale - reference.scale) * weight;
}

void prepareBlendOutput(const Pose& from, size_t blendedCount, Pose& out) {
    if (&out == &from) return;
    out.joints.resize(from.joints.size());
    std::copy(from.joints.begin() + static_cast<std::ptrdiff_t>(blendedCount), from.joints.end(), out.joints.begin() + static_cast<std::ptrdiff_t>(blendedCount));
}

float smoothstep01(float value) { return value * value * (3.0f - 2.0f * value); }

}

int Skeleton::add(const std::string& name, int parent, const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    joints.push_back({name, parent, position, rotation, scale});
    return static_cast<int>(joints.size() - 1);
}

int Skeleton::find(const std::string& name) const {
    for (size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex) {
        if (joints[jointIndex].name == name) return static_cast<int>(jointIndex);
    }
    return -1;
}

Pose Pose::bind(const Skeleton& skeleton) {
    Pose pose;
    pose.joints.reserve(skeleton.joints.size());
    for (const Joint& joint : skeleton.joints) pose.joints.push_back({joint.position, joint.rotation, joint.scale});
    return pose;
}

void Pose::assignBind(const Skeleton& skeleton) {
    joints.resize(skeleton.joints.size());
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
        const Joint& joint = skeleton.joints[jointIndex];
        joints[jointIndex] = {joint.position, joint.rotation, joint.scale};
    }
}

uint32_t SkeletalLibrary::add(SkeletalClip clip) {
    const bool deriveDuration = clip.duration <= 0.0f;
    const auto byTime = [](const auto& lhs, const auto& rhs) { return lhs.time < rhs.time; };
    for (JointTrack& track : clip.tracks) {
        std::stable_sort(track.position.begin(), track.position.end(), byTime);
        std::stable_sort(track.rotation.begin(), track.rotation.end(), byTime);
        std::stable_sort(track.scale.begin(), track.scale.end(), byTime);
        if (!deriveDuration) continue;
        if (!track.position.empty()) clip.duration = std::max(clip.duration, track.position.back().time);
        if (!track.rotation.empty()) clip.duration = std::max(clip.duration, track.rotation.back().time);
        if (!track.scale.empty()) clip.duration = std::max(clip.duration, track.scale.back().time);
    }
    m_clips.push_back(std::move(clip));
    return static_cast<uint32_t>(m_clips.size() - 1);
}

int SkeletalLibrary::find(const std::string& name) const {
    for (size_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex) {
        if (m_clips[clipIndex].name == name) return static_cast<int>(clipIndex);
    }
    return -1;
}

void sampleSkeletalClip(const Skeleton& skeleton, const SkeletalClip& clip, float time, Pose& out) {
    out.assignBind(skeleton);
    float clipTime = time;
    if (clip.duration > 0.0f) {
        if (clip.loop) {
            clipTime = std::fmod(clipTime, clip.duration);
            if (clipTime < 0.0f) clipTime += clip.duration;
        } else {
            clipTime = std::clamp(clipTime, 0.0f, clip.duration);
        }
    }
    const auto blendRotation = [](const glm::quat& from, const glm::quat& to, float fraction) { return nlerp(from, to, fraction); };
    for (const JointTrack& track : clip.tracks) {
        if (!validJoint(skeleton, track.joint)) continue;
        JointPose& joint = out.joints[track.joint];
        if (!track.position.empty()) joint.position = sampleKeys(track.position, clipTime, lerpVec3);
        if (!track.rotation.empty()) joint.rotation = sampleKeys(track.rotation, clipTime, blendRotation);
        if (!track.scale.empty()) joint.scale = sampleKeys(track.scale, clipTime, lerpVec3);
    }
}

void blendPoses(const Pose& from, const Pose& to, float weight, Pose& out) {
    const size_t jointCount = std::min(from.joints.size(), to.joints.size());
    prepareBlendOutput(from, jointCount, out);
    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) blendJoint(from.joints[jointIndex], to.joints[jointIndex], weight, out.joints[jointIndex]);
}

void blendPosesMasked(const Pose& from, const Pose& to, const std::vector<float>& mask, float weight, Pose& out) {
    const size_t jointCount = std::min(from.joints.size(), to.joints.size());
    prepareBlendOutput(from, jointCount, out);
    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        const float jointWeight = weight * (jointIndex < mask.size() ? mask[jointIndex] : 1.0f);
        blendJoint(from.joints[jointIndex], to.joints[jointIndex], jointWeight, out.joints[jointIndex]);
    }
}

void addPose(Pose& base, const Pose& layer, const Pose& reference, float weight) {
    const size_t jointCount = std::min({base.joints.size(), layer.joints.size(), reference.joints.size()});
    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) addJoint(base.joints[jointIndex], layer.joints[jointIndex], reference.joints[jointIndex], weight);
}

std::vector<float> subtreeMask(const Skeleton& skeleton, int rootJoint, bool includeRoot) {
    std::vector<float> mask(skeleton.joints.size(), 0.0f);
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
        for (int ancestor = static_cast<int>(jointIndex); ancestor >= 0; ancestor = parentOf(skeleton, static_cast<size_t>(ancestor))) {
            if (ancestor != rootJoint) continue;
            mask[jointIndex] = (static_cast<int>(jointIndex) == rootJoint && !includeRoot) ? 0.0f : 1.0f;
            break;
        }
    }
    return mask;
}

ModelPose computeModelPose(const Skeleton& skeleton, const Pose& pose) {
    ModelPose model;
    computeModelPose(skeleton, pose, model);
    return model;
}

void computeModelPose(const Skeleton& skeleton, const Pose& pose, ModelPose& model) {
    const size_t jointCount = skeleton.joints.size();
    model.position.resize(jointCount);
    model.rotation.resize(jointCount);
    const size_t posedCount = std::min(jointCount, pose.joints.size());
    std::fill(model.position.begin() + static_cast<std::ptrdiff_t>(posedCount), model.position.end(), glm::vec3());
    std::fill(model.rotation.begin() + static_cast<std::ptrdiff_t>(posedCount), model.rotation.end(), glm::quat());
    for (size_t jointIndex = 0; jointIndex < posedCount; ++jointIndex) {
        const int parent = parentOf(skeleton, jointIndex);
        const JointPose& joint = pose.joints[jointIndex];
        if (parent < 0) {
            model.position[jointIndex] = joint.position;
            model.rotation[jointIndex] = joint.rotation;
            continue;
        }
        const glm::vec3 scaledPosition = joint.position * pose.joints[parent].scale;
        model.position[jointIndex] = model.position[parent] + model.rotation[parent] * scaledPosition;
        model.rotation[jointIndex] = model.rotation[parent] * joint.rotation;
    }
}

std::vector<glm::mat4> computeModelMatrices(const Skeleton& skeleton, const Pose& pose) {
    std::vector<glm::mat4> matrices;
    computeModelMatrices(skeleton, pose, matrices);
    return matrices;
}

void computeModelMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& matrices) {
    matrices.resize(skeleton.joints.size());
    const size_t posedCount = std::min(matrices.size(), pose.joints.size());
    std::fill(matrices.begin() + static_cast<std::ptrdiff_t>(posedCount), matrices.end(), glm::mat4());
    for (size_t jointIndex = 0; jointIndex < posedCount; ++jointIndex) {
        const glm::mat4 local = localMatrix(pose.joints[jointIndex]);
        const int parent = parentOf(skeleton, jointIndex);
        matrices[jointIndex] = parent < 0 ? local : matrices[parent] * local;
    }
}

void applyPoseToEntities(World& world, const std::vector<EntityHandle>& jointEntities, const Pose& pose) {
    const size_t jointCount = std::min(jointEntities.size(), pose.joints.size());
    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) world.setLocalMatrix(jointEntities[jointIndex], localMatrix(pose.joints[jointIndex]));
}

void SkeletalAnimator::play(uint32_t clip, float fadeSeconds) {
    if (m_current.valid && m_current.space.empty() && m_current.clip == clip) return;
    m_previous = m_current;
    m_current = Source{true, clip, {}, 0.0f};
    m_fadeTotal = m_fadeLeft = m_previous.valid ? fadeSeconds : 0.0f;
}

void SkeletalAnimator::playBlendSpace(std::vector<BlendSample> samples, float parameter, float fadeSeconds) {
    std::sort(samples.begin(), samples.end(), [](const BlendSample& lhs, const BlendSample& rhs) { return lhs.x < rhs.x; });
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

void SkeletalAnimator::advance(Source& source, float dt) const { source.time += dt * m_speed; }

void SkeletalAnimator::evaluateBlendSpace(const Source& source, Pose& out) {
    const std::vector<BlendSample>& samples = source.space;
    const float parameter = std::clamp(std::isfinite(m_parameter) ? m_parameter : samples.front().x, samples.front().x, samples.back().x);
    size_t upper = 1;
    while (upper < samples.size() && samples[upper].x < parameter) ++upper;
    if (samples.size() == 1 || upper >= samples.size()) {
        const SkeletalClip* clip = m_library->get(samples.back().clip);
        if (clip) sampleSkeletalClip(*m_skeleton, *clip, source.time, out);
        else out.assignBind(*m_skeleton);
        return;
    }
    const BlendSample& lower = samples[upper - 1];
    const BlendSample& higher = samples[upper];
    const SkeletalClip* lowerClip = m_library->get(lower.clip);
    const SkeletalClip* higherClip = m_library->get(higher.clip);
    if (!lowerClip || !higherClip) {
        out.assignBind(*m_skeleton);
        return;
    }
    const float fraction = higher.x > lower.x ? (parameter - lower.x) / (higher.x - lower.x) : 0.0f;
    const float lowerDuration = lowerClip->duration > 0.0f ? lowerClip->duration : DEFAULT_BLEND_DURATION;
    const float higherDuration = higherClip->duration > 0.0f ? higherClip->duration : DEFAULT_BLEND_DURATION;
    const float phase = std::fmod(source.time / (lowerDuration + (higherDuration - lowerDuration) * fraction), 1.0f);
    sampleSkeletalClip(*m_skeleton, *lowerClip, phase * lowerDuration, m_scratchLower);
    sampleSkeletalClip(*m_skeleton, *higherClip, phase * higherDuration, m_scratchHigher);
    blendPoses(m_scratchLower, m_scratchHigher, fraction, out);
}

void SkeletalAnimator::evaluate(const Source& source, Pose& out) {
    if (!source.valid) {
        out.assignBind(*m_skeleton);
        return;
    }
    if (!source.space.empty()) {
        evaluateBlendSpace(source, out);
        return;
    }
    const SkeletalClip* clip = m_library->get(source.clip);
    if (clip) sampleSkeletalClip(*m_skeleton, *clip, source.time, out);
    else out.assignBind(*m_skeleton);
}

void SkeletalAnimator::applyFade(float dt, Pose& current) {
    if (m_fadeLeft <= 0.0f || !m_previous.valid) return;
    m_fadeLeft = std::max(0.0f, m_fadeLeft - dt);
    evaluate(m_previous, m_scratchPrevious);
    const float progress = m_fadeTotal > 0.0f ? 1.0f - m_fadeLeft / m_fadeTotal : 1.0f;
    blendPoses(m_scratchPrevious, current, smoothstep01(progress), current);
    if (m_fadeLeft == 0.0f) m_previous.valid = false;
}

void SkeletalAnimator::applyOverlay(Pose& current) {
    if (!m_overlayActive) return;
    Pose& overlay = m_scratchOverlay;
    evaluate(m_overlay, overlay);
    if (!m_overlayAdditive) {
        blendPosesMasked(current, overlay, m_overlayMask, m_overlayWeight, current);
        return;
    }
    m_scratchReference.assignBind(*m_skeleton);
    const Pose& reference = m_scratchReference;
    const size_t jointCount = std::min({current.joints.size(), overlay.joints.size(), reference.joints.size()});
    for (size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
        const float maskWeight = jointIndex < m_overlayMask.size() ? m_overlayMask[jointIndex] : 1.0f;
        if (maskWeight <= 0.0f) continue;
        addJoint(current.joints[jointIndex], overlay.joints[jointIndex], reference.joints[jointIndex], m_overlayWeight * maskWeight);
    }
}

void SkeletalAnimator::update(float dt) {
    advance(m_current, dt);
    if (m_previous.valid) advance(m_previous, dt);
    if (m_overlayActive) advance(m_overlay, dt);
    evaluate(m_current, m_pose);
    applyFade(dt, m_pose);
    applyOverlay(m_pose);
}
