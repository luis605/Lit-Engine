module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

module Engine.Face;

const std::array<const char*, kBlendshapeCount> kArkitBlendshapes = {
    "eyeBlinkLeft",   "eyeLookDownLeft",  "eyeLookInLeft",    "eyeLookOutLeft",   "eyeLookUpLeft",   "eyeSquintLeft",   "eyeWideLeft",
    "eyeBlinkRight",  "eyeLookDownRight", "eyeLookInRight",   "eyeLookOutRight",  "eyeLookUpRight",  "eyeSquintRight",  "eyeWideRight",
    "jawForward",     "jawLeft",          "jawRight",         "jawOpen",          "mouthClose",      "mouthFunnel",     "mouthPucker",
    "mouthLeft",      "mouthRight",       "mouthSmileLeft",   "mouthSmileRight",  "mouthFrownLeft",  "mouthFrownRight", "mouthDimpleLeft",
    "mouthDimpleRight", "mouthStretchLeft", "mouthStretchRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper",
    "mouthPressLeft", "mouthPressRight",  "mouthLowerDownLeft", "mouthLowerDownRight", "mouthUpperUpLeft", "mouthUpperUpRight", "browDownLeft", "browDownRight",
    "browInnerUp",    "browOuterUpLeft",  "browOuterUpRight", "cheekPuff",        "cheekSquintLeft", "cheekSquintRight", "noseSneerLeft",
    "noseSneerRight", "tongueOut"};

int blendshapeIndex(std::string_view name) {
    for (size_t index = 0; index < kBlendshapeCount; ++index) {
        if (name == kArkitBlendshapes[index]) return static_cast<int>(index);
    }
    return -1;
}

BlendshapeWeights blendshapeZero() {
    BlendshapeWeights weights;
    weights.fill(0.0f);
    return weights;
}

namespace {
constexpr float MIN_KEY_SPAN = 1.0e-8f;
constexpr float RADIANS_TO_DEGREES = 180.0f / 3.14159265f;
constexpr float SACCADE_PITCH_SCALE = 0.6f;
constexpr float DOUBLE_BLINK_GAP_SCALE = 1.5f;
constexpr float BLINK_CLOSE_FRACTION = 0.4f;
constexpr float MIN_BLINK_DURATION = 1.0e-4f;
constexpr float MIN_EYE_RANGE = 1.0e-4f;

float approach(float current, float goal, float maxStep) {
    const float difference = goal - current;
    return std::fabs(difference) <= maxStep ? goal : current + (difference > 0.0f ? maxStep : -maxStep);
}

float sampleCurve(const std::vector<FloatKey>& keys, float time) {
    const auto next = std::upper_bound(keys.begin(), keys.end(), time, [](float value, const FloatKey& key) { return value < key.time; });
    if (next == keys.end()) return keys.back().value;
    if (next == keys.begin()) return next->value;
    const FloatKey& previous = *(next - 1);
    const float span = next->time - previous.time;
    const float fraction = span > MIN_KEY_SPAN ? (time - previous.time) / span : 1.0f;
    return previous.value + (next->value - previous.value) * fraction;
}

bool parseCurveKeys(const Json& keys, float framesPerSecond, const std::string& channel, BlendshapeCurve& curve, std::string* error) {
    int frame = 0;
    for (const Json& key : keys.items()) {
        if (framesPerSecond > 0.0f && key.isNumber()) {
            curve.keys.push_back({static_cast<float>(frame) / framesPerSecond, static_cast<float>(key.asNumber())});
        } else {
            const Json* time = key.at(0);
            const Json* value = key.at(1);
            if (!time || !value) {
                if (error) *error = "bad key in " + channel;
                return false;
            }
            curve.keys.push_back({static_cast<float>(time->asNumber()), static_cast<float>(value->asNumber())});
        }
        ++frame;
    }
    return true;
}

struct EyeChannels {
    int lookOutLeft = blendshapeIndex("eyeLookOutLeft");
    int lookInLeft = blendshapeIndex("eyeLookInLeft");
    int lookInRight = blendshapeIndex("eyeLookInRight");
    int lookOutRight = blendshapeIndex("eyeLookOutRight");
    int lookUpLeft = blendshapeIndex("eyeLookUpLeft");
    int lookUpRight = blendshapeIndex("eyeLookUpRight");
    int lookDownLeft = blendshapeIndex("eyeLookDownLeft");
    int lookDownRight = blendshapeIndex("eyeLookDownRight");
    int blinkLeft = blendshapeIndex("eyeBlinkLeft");
    int blinkRight = blendshapeIndex("eyeBlinkRight");
};
}

bool BlendshapeClip::loadJson(const Json& doc, std::string* error) {
    BlendshapeClip next;
    if (const Json* nameField = doc.get("name")) next.name = nameField->asString();
    if (const Json* loopField = doc.get("loop")) next.loop = loopField->asBool();
    const Json* curvesField = doc.get("curves");
    if (!curvesField || !curvesField->isObject()) {
        if (error) *error = "missing curves object";
        return false;
    }
    const Json* framesField = doc.get("fps");
    const float framesPerSecond = framesField ? static_cast<float>(framesField->asNumber()) : 0.0f;
    for (const auto& [channel, keys] : curvesField->members()) {
        const int channelIndex = blendshapeIndex(channel);
        if (channelIndex < 0) {
            if (error) *error = "unknown blendshape " + channel;
            return false;
        }
        BlendshapeCurve curve;
        curve.channel = channelIndex;
        if (!parseCurveKeys(keys, framesPerSecond, channel, curve, error)) return false;
        std::stable_sort(curve.keys.begin(), curve.keys.end(), [](const FloatKey& lhs, const FloatKey& rhs) { return lhs.time < rhs.time; });
        if (!curve.keys.empty()) next.duration = std::max(next.duration, curve.keys.back().time);
        next.curves.push_back(std::move(curve));
    }
    if (const Json* durationField = doc.get("duration")) next.duration = std::max(next.duration, static_cast<float>(durationField->asNumber()));
    *this = std::move(next);
    return true;
}

bool BlendshapeClip::loadText(std::string_view text, std::string* error) {
    const auto doc = Json::parse(text, error);
    return doc && loadJson(*doc, error);
}

Json BlendshapeClip::toJson() const {
    Json result = Json::object();
    result.set("name", name);
    result.set("loop", loop);
    result.set("duration", duration);
    Json curvesJson = Json::object();
    for (const BlendshapeCurve& curve : curves) {
        Json keysJson = Json::array();
        for (const FloatKey& key : curve.keys) {
            Json pair = Json::array();
            pair.push(key.time);
            pair.push(key.value);
            keysJson.push(std::move(pair));
        }
        curvesJson.set(kArkitBlendshapes[curve.channel], std::move(keysJson));
    }
    result.set("curves", std::move(curvesJson));
    return result;
}

void BlendshapeClip::sample(float time, BlendshapeWeights& out) const {
    float wrappedTime = time;
    if (duration > 0.0f) {
        if (loop) {
            wrappedTime = std::fmod(wrappedTime, duration);
            if (wrappedTime < 0.0f) wrappedTime += duration;
        } else {
            wrappedTime = std::clamp(wrappedTime, 0.0f, duration);
        }
    }
    for (const BlendshapeCurve& curve : curves) {
        if (curve.keys.empty()) continue;
        out[curve.channel] = std::clamp(sampleCurve(curve.keys, wrappedTime), 0.0f, 1.0f);
    }
}

uint32_t BlendshapeLibrary::add(BlendshapeClip clip) {
    m_clips.push_back(std::move(clip));
    return static_cast<uint32_t>(m_clips.size() - 1);
}

int BlendshapeLibrary::find(const std::string& name) const {
    for (size_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex) {
        if (m_clips[clipIndex].name == name) return static_cast<int>(clipIndex);
    }
    return -1;
}

void BlendshapePlayer::play(uint32_t clip, float speed) {
    m_clip = clip;
    m_speed = speed;
    m_time = 0.0f;
    m_playing = true;
}

void BlendshapePlayer::update(float dt, const BlendshapeLibrary& library, BlendshapeWeights& out) {
    if (!m_playing) return;
    const BlendshapeClip* clip = library.get(m_clip);
    if (!clip) {
        m_playing = false;
        return;
    }
    m_time += dt * m_speed;
    if (!clip->loop && m_time >= clip->duration) {
        m_time = clip->duration;
        m_playing = false;
    }
    clip->sample(m_time, out);
}

void blendshapeMax(BlendshapeWeights& base, const BlendshapeWeights& layer) {
    for (size_t channel = 0; channel < kBlendshapeCount; ++channel) base[channel] = std::max(base[channel], layer[channel]);
}

void blendshapeAdd(BlendshapeWeights& base, const BlendshapeWeights& layer, float weight) {
    for (size_t channel = 0; channel < kBlendshapeCount; ++channel) base[channel] = std::clamp(base[channel] + layer[channel] * weight, 0.0f, 1.0f);
}

void EyeSystem::update(float dt, Pcg32& rng) {
    float goalYaw = 0.0f;
    float goalPitch = 0.0f;
    if (m_hasTarget) {
        const glm::vec3 local = glm::inverse(m_head) * (m_target - m_eye);
        const float flat = std::sqrt(local.x * local.x + local.z * local.z);
        goalYaw = std::clamp(std::atan2(local.x, local.z) * RADIANS_TO_DEGREES, -m_settings.maxYawDegrees, m_settings.maxYawDegrees);
        goalPitch = std::clamp(std::atan2(local.y, flat) * RADIANS_TO_DEGREES, -m_settings.maxPitchDegrees, m_settings.maxPitchDegrees);
    }
    const float shift = std::fabs(goalYaw - m_yaw) + std::fabs(goalPitch - m_pitch);
    if (shift > m_settings.shiftBlinkDegrees && !m_blinking && rng.chance(m_settings.shiftBlinkChance)) requestBlink();
    const float gazeStep = m_settings.gazeSpeedDegrees * dt;
    m_yaw = approach(m_yaw, goalYaw, gazeStep);
    m_pitch = approach(m_pitch, goalPitch, gazeStep);

    updateSaccade(dt, rng);
    updateBlink(dt, rng);
}

void EyeSystem::updateSaccade(float dt, Pcg32& rng) {
    if (m_saccadeTimer < 0.0f) m_saccadeTimer = rng.range(m_settings.saccadeIntervalMin, m_settings.saccadeIntervalMax);
    m_saccadeTimer -= dt;
    if (m_saccadeTimer <= 0.0f) {
        const float amplitude = m_settings.saccadeAmplitudeDegrees;
        m_saccadeGoalYaw = rng.range(-amplitude, amplitude);
        m_saccadeGoalPitch = rng.range(-amplitude, amplitude) * SACCADE_PITCH_SCALE;
        m_saccadeTimer = rng.range(m_settings.saccadeIntervalMin, m_settings.saccadeIntervalMax);
        ++m_saccadeCount;
    }
    const float saccadeStep = m_settings.saccadeSpeedDegrees * dt;
    m_saccadeYaw = approach(m_saccadeYaw, m_saccadeGoalYaw, saccadeStep);
    m_saccadePitch = approach(m_saccadePitch, m_saccadeGoalPitch, saccadeStep);
}

void EyeSystem::updateBlink(float dt, Pcg32& rng) {
    if (m_blinkTimer < 0.0f) m_blinkTimer = rng.range(m_settings.blinkIntervalMin, m_settings.blinkIntervalMax);
    if (!m_blinking) {
        m_blinkTimer -= dt;
        if (m_blinkTimer <= 0.0f) {
            requestBlink();
            m_blinkTimer = rng.chance(m_settings.doubleBlinkChance) ? m_settings.blinkDuration * DOUBLE_BLINK_GAP_SCALE
                                                                    : rng.range(m_settings.blinkIntervalMin, m_settings.blinkIntervalMax);
        }
    }
    m_blinkValue = 0.0f;
    if (!m_blinking) return;
    m_blinkTime += dt;
    const float progress = m_blinkTime / std::max(m_settings.blinkDuration, MIN_BLINK_DURATION);
    if (progress >= 1.0f) {
        m_blinking = false;
        ++m_blinkCount;
        return;
    }
    const float linear = progress < BLINK_CLOSE_FRACTION ? progress / BLINK_CLOSE_FRACTION : 1.0f - (progress - BLINK_CLOSE_FRACTION) / (1.0f - BLINK_CLOSE_FRACTION);
    m_blinkValue = linear * linear * (3.0f - 2.0f * linear);
}

void EyeSystem::applyTo(BlendshapeWeights& weights) const {
    static const EyeChannels channels;
    const float yaw = yawDegrees() / std::max(m_settings.maxYawDegrees, MIN_EYE_RANGE);
    const float pitch = pitchDegrees() / std::max(m_settings.maxPitchDegrees, MIN_EYE_RANGE);
    const float left = std::clamp(yaw, 0.0f, 1.0f);
    const float right = std::clamp(-yaw, 0.0f, 1.0f);
    const float up = std::clamp(pitch, 0.0f, 1.0f);
    const float down = std::clamp(-pitch, 0.0f, 1.0f);
    weights[channels.lookOutLeft] = left;
    weights[channels.lookInLeft] = right;
    weights[channels.lookInRight] = left;
    weights[channels.lookOutRight] = right;
    weights[channels.lookUpLeft] = up;
    weights[channels.lookUpRight] = up;
    weights[channels.lookDownLeft] = down;
    weights[channels.lookDownRight] = down;
    weights[channels.blinkLeft] = std::max(weights[channels.blinkLeft], m_blinkValue);
    weights[channels.blinkRight] = std::max(weights[channels.blinkRight], m_blinkValue);
}
