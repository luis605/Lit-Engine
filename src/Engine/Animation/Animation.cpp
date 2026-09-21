module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

module Engine.Animation;

namespace {
template <typename Key>
const Key* keyAfter(const std::vector<Key>& keys, float t) {
    const auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float value, const Key& key) { return value < key.time; });
    return it == keys.end() ? nullptr : &*it;
}

glm::vec3 sampleVec3(const std::vector<Vec3Key>& keys, float t) {
    const Vec3Key* next = keyAfter(keys, t);
    if (!next) return keys.back().value;
    if (next == keys.data()) return next->value;
    const Vec3Key& prev = *(next - 1);
    const float span = next->time - prev.time;
    const float f = span > 1.0e-8f ? (t - prev.time) / span : 1.0f;
    return prev.value + (next->value - prev.value) * f;
}

glm::quat sampleQuat(const std::vector<QuatKey>& keys, float t) {
    const QuatKey* next = keyAfter(keys, t);
    if (!next) return keys.back().value;
    if (next == keys.data()) return next->value;
    const QuatKey& prev = *(next - 1);
    const float span = next->time - prev.time;
    const float f = span > 1.0e-8f ? (t - prev.time) / span : 1.0f;
    return glm::normalize(glm::slerp(prev.value, next->value, f));
}

glm::mat4 compose(const glm::vec3& p, const glm::quat& r, const glm::vec3& s) {
    return glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}
}

float AnimationClip::duration() const {
    float d = 0.0f;
    if (!position.empty()) d = std::max(d, position.back().time);
    if (!rotation.empty()) d = std::max(d, rotation.back().time);
    if (!scale.empty()) d = std::max(d, scale.back().time);
    return d;
}

uint32_t AnimationLibrary::add(AnimationClip clip) {
    const auto byTime = [](const auto& a, const auto& b) { return a.time < b.time; };
    std::stable_sort(clip.position.begin(), clip.position.end(), byTime);
    std::stable_sort(clip.rotation.begin(), clip.rotation.end(), byTime);
    std::stable_sort(clip.scale.begin(), clip.scale.end(), byTime);
    m_clips.push_back(std::move(clip));
    return static_cast<uint32_t>(m_clips.size() - 1);
}

const AnimationClip* AnimationLibrary::get(uint32_t id) const { return id < m_clips.size() ? &m_clips[id] : nullptr; }

uint32_t AnimationLibrary::find(const std::string& name) const {
    for (uint32_t i = 0; i < m_clips.size(); ++i) {
        if (m_clips[i].name == name) return i;
    }
    return INVALID_ENTITY;
}

void updateAnimators(World& world, const AnimationLibrary& library, float dt) {
    world.view<Animator>([&](EntityHandle e, Animator& animator) {
        if (!animator.playing) return;
        const AnimationClip* clip = library.get(animator.clip);
        if (!clip) return;
        const float duration = clip->duration();

        animator.time += dt * animator.speed;
        float t = animator.time;
        if (duration > 0.0f) {
            if (clip->loop) {
                t = std::fmod(t, duration);
                if (t < 0.0f) t += duration;
                animator.time = t;
            } else if (t >= duration) {
                t = duration;
                animator.time = duration;
                animator.playing = false;
            } else if (t < 0.0f) {
                t = 0.0f;
                animator.time = 0.0f;
                animator.playing = false;
            }
        } else {
            t = 0.0f;
        }

        const glm::vec3 position = clip->position.empty() ? world.getPosition(e) : sampleVec3(clip->position, t);
        const glm::quat rotation = clip->rotation.empty() ? world.getRotation(e) : sampleQuat(clip->rotation, t);
        const glm::vec3 scale = clip->scale.empty() ? world.getScale(e) : sampleVec3(clip->scale, t);
        world.setLocalMatrix(e, compose(position, rotation, scale));
    });
}

void registerAnimationComponents(World& world) {
    world.registerComponent<Animator>(
        "Animator", [](const Animator& a, std::ostream& out) { out << a.clip << ' ' << a.time << ' ' << a.speed << ' ' << int(a.playing); },
        [](std::istream& in) -> std::optional<Animator> {
            Animator a;
            int playing;
            if (!(in >> a.clip >> a.time >> a.speed >> playing)) return std::nullopt;
            a.playing = playing != 0;
            return a;
        });
}
