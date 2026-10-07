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
    for (size_t i = 0; i < kBlendshapeCount; ++i) {
        if (name == kArkitBlendshapes[i]) return static_cast<int>(i);
    }
    return -1;
}

BlendshapeWeights blendshapeZero() {
    BlendshapeWeights w;
    w.fill(0.0f);
    return w;
}

bool BlendshapeClip::loadJson(const Json& doc, std::string* error) {
    BlendshapeClip next;
    if (const Json* n = doc.get("name")) next.name = n->asString();
    if (const Json* l = doc.get("loop")) next.loop = l->asBool();
    const Json* curves = doc.get("curves");
    if (!curves || !curves->isObject()) {
        if (error) *error = "missing curves object";
        return false;
    }
    const float fps = doc.get("fps") ? static_cast<float>(doc.get("fps")->asNumber()) : 0.0f;
    for (const auto& [chan, keys] : curves->members()) {
        const int index = blendshapeIndex(chan);
        if (index < 0) {
            if (error) *error = "unknown blendshape " + chan;
            return false;
        }
        BlendshapeCurve curve;
        curve.channel = index;
        int i = 0;
        for (const Json& k : keys.items()) {
            if (fps > 0.0f && k.isNumber()) {
                curve.keys.push_back({static_cast<float>(i) / fps, static_cast<float>(k.asNumber())});
            } else {
                const Json* t = k.at(0);
                const Json* v = k.at(1);
                if (!t || !v) {
                    if (error) *error = "bad key in " + chan;
                    return false;
                }
                curve.keys.push_back({static_cast<float>(t->asNumber()), static_cast<float>(v->asNumber())});
            }
            ++i;
        }
        std::stable_sort(curve.keys.begin(), curve.keys.end(), [](const FloatKey& a, const FloatKey& b) { return a.time < b.time; });
        if (!curve.keys.empty()) next.duration = std::max(next.duration, curve.keys.back().time);
        next.curves.push_back(std::move(curve));
    }
    if (const Json* d = doc.get("duration")) next.duration = std::max(next.duration, static_cast<float>(d->asNumber()));
    *this = std::move(next);
    return true;
}

bool BlendshapeClip::loadText(std::string_view text, std::string* error) {
    const auto doc = Json::parse(text, error);
    return doc && loadJson(*doc, error);
}

Json BlendshapeClip::toJson() const {
    Json out = Json::object();
    out.set("name", name);
    out.set("loop", loop);
    out.set("duration", duration);
    Json cs = Json::object();
    for (const BlendshapeCurve& c : curves) {
        Json arr = Json::array();
        for (const FloatKey& k : c.keys) {
            Json pair = Json::array();
            pair.push(k.time);
            pair.push(k.value);
            arr.push(std::move(pair));
        }
        cs.set(kArkitBlendshapes[c.channel], std::move(arr));
    }
    out.set("curves", std::move(cs));
    return out;
}

void BlendshapeClip::sample(float time, BlendshapeWeights& out) const {
    float t = time;
    if (duration > 0.0f) {
        if (loop) {
            t = std::fmod(t, duration);
            if (t < 0.0f) t += duration;
        } else {
            t = std::clamp(t, 0.0f, duration);
        }
    }
    for (const BlendshapeCurve& c : curves) {
        if (c.keys.empty()) continue;
        const auto it = std::upper_bound(c.keys.begin(), c.keys.end(), t, [](float v, const FloatKey& k) { return v < k.time; });
        float value;
        if (it == c.keys.end()) value = c.keys.back().value;
        else if (it == c.keys.begin()) value = it->value;
        else {
            const FloatKey& p = *(it - 1);
            const float span = it->time - p.time;
            const float f = span > 1.0e-8f ? (t - p.time) / span : 1.0f;
            value = p.value + (it->value - p.value) * f;
        }
        out[c.channel] = std::clamp(value, 0.0f, 1.0f);
    }
}

uint32_t BlendshapeLibrary::add(BlendshapeClip clip) {
    m_clips.push_back(std::move(clip));
    return static_cast<uint32_t>(m_clips.size() - 1);
}

int BlendshapeLibrary::find(const std::string& name) const {
    for (size_t i = 0; i < m_clips.size(); ++i) {
        if (m_clips[i].name == name) return static_cast<int>(i);
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
    for (size_t i = 0; i < kBlendshapeCount; ++i) base[i] = std::max(base[i], layer[i]);
}

void blendshapeAdd(BlendshapeWeights& base, const BlendshapeWeights& layer, float weight) {
    for (size_t i = 0; i < kBlendshapeCount; ++i) base[i] = std::clamp(base[i] + layer[i] * weight, 0.0f, 1.0f);
}

namespace {
float approach(float cur, float goal, float maxStep) {
    const float d = goal - cur;
    return std::fabs(d) <= maxStep ? goal : cur + (d > 0.0f ? maxStep : -maxStep);
}
}

void EyeSystem::update(float dt, Pcg32& rng) {
    const float rad = 180.0f / 3.14159265f;
    float goalYaw = 0.0f;
    float goalPitch = 0.0f;
    if (m_hasTarget) {
        const glm::vec3 local = glm::inverse(m_head) * (m_target - m_eye);
        const float flat = std::sqrt(local.x * local.x + local.z * local.z);
        goalYaw = std::atan2(local.x, local.z) * rad;
        goalPitch = std::atan2(local.y, flat) * rad;
        goalYaw = std::clamp(goalYaw, -m_settings.maxYawDegrees, m_settings.maxYawDegrees);
        goalPitch = std::clamp(goalPitch, -m_settings.maxPitchDegrees, m_settings.maxPitchDegrees);
    }
    const float shift = std::fabs(goalYaw - m_yaw) + std::fabs(goalPitch - m_pitch);
    if (shift > m_settings.shiftBlinkDegrees && !m_blinking && rng.chance(m_settings.shiftBlinkChance)) requestBlink();
    const float step = m_settings.gazeSpeedDegrees * dt;
    m_yaw = approach(m_yaw, goalYaw, step);
    m_pitch = approach(m_pitch, goalPitch, step);

    if (m_saccadeTimer < 0.0f) m_saccadeTimer = rng.range(m_settings.saccadeIntervalMin, m_settings.saccadeIntervalMax);
    m_saccadeTimer -= dt;
    if (m_saccadeTimer <= 0.0f) {
        const float a = m_settings.saccadeAmplitudeDegrees;
        m_saccadeGoalYaw = rng.range(-a, a);
        m_saccadeGoalPitch = rng.range(-a, a) * 0.6f;
        m_saccadeTimer = rng.range(m_settings.saccadeIntervalMin, m_settings.saccadeIntervalMax);
        ++m_saccadeCount;
    }
    const float sstep = m_settings.saccadeSpeedDegrees * dt;
    m_saccadeYaw = approach(m_saccadeYaw, m_saccadeGoalYaw, sstep);
    m_saccadePitch = approach(m_saccadePitch, m_saccadeGoalPitch, sstep);

    if (m_blinkTimer < 0.0f) m_blinkTimer = rng.range(m_settings.blinkIntervalMin, m_settings.blinkIntervalMax);
    if (!m_blinking) {
        m_blinkTimer -= dt;
        if (m_blinkTimer <= 0.0f) {
            requestBlink();
            m_blinkTimer = rng.chance(m_settings.doubleBlinkChance) ? m_settings.blinkDuration * 1.5f : rng.range(m_settings.blinkIntervalMin, m_settings.blinkIntervalMax);
        }
    }
    m_blinkValue = 0.0f;
    if (m_blinking) {
        m_blinkTime += dt;
        const float u = m_blinkTime / std::max(m_settings.blinkDuration, 1.0e-4f);
        if (u >= 1.0f) {
            m_blinking = false;
            ++m_blinkCount;
        } else {
            m_blinkValue = u < 0.4f ? u / 0.4f : 1.0f - (u - 0.4f) / 0.6f;
            m_blinkValue = m_blinkValue * m_blinkValue * (3.0f - 2.0f * m_blinkValue);
        }
    }
}

void EyeSystem::applyTo(BlendshapeWeights& w) const {
    const float yaw = yawDegrees() / m_settings.maxYawDegrees;
    const float pitch = pitchDegrees() / m_settings.maxPitchDegrees;
    const float left = std::clamp(yaw, 0.0f, 1.0f);
    const float right = std::clamp(-yaw, 0.0f, 1.0f);
    const float up = std::clamp(pitch, 0.0f, 1.0f);
    const float down = std::clamp(-pitch, 0.0f, 1.0f);
    w[blendshapeIndex("eyeLookOutLeft")] = left;
    w[blendshapeIndex("eyeLookInLeft")] = right;
    w[blendshapeIndex("eyeLookInRight")] = left;
    w[blendshapeIndex("eyeLookOutRight")] = right;
    w[blendshapeIndex("eyeLookUpLeft")] = up;
    w[blendshapeIndex("eyeLookUpRight")] = up;
    w[blendshapeIndex("eyeLookDownLeft")] = down;
    w[blendshapeIndex("eyeLookDownRight")] = down;
    w[blendshapeIndex("eyeBlinkLeft")] = std::max(w[blendshapeIndex("eyeBlinkLeft")], m_blinkValue);
    w[blendshapeIndex("eyeBlinkRight")] = std::max(w[blendshapeIndex("eyeBlinkRight")], m_blinkValue);
}
