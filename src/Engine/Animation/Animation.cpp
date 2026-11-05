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
constexpr float MIN_KEY_SPAN = 1.0e-8f;

template <typename Key>
const Key* keyAfter(const std::vector<Key>& keys, float time) {
    const auto found = std::upper_bound(keys.begin(), keys.end(), time, [](float value, const Key& key) { return value < key.time; });
    return found == keys.end() ? nullptr : &*found;
}

template <typename Key, typename Blend>
auto sampleKeys(const std::vector<Key>& keys, float time, Blend blend) {
    const Key* next = keyAfter(keys, time);
    if (!next) return keys.back().value;
    if (next == keys.data()) return next->value;
    const Key& previous = *(next - 1);
    const float span = next->time - previous.time;
    const float fraction = span > MIN_KEY_SPAN ? (time - previous.time) / span : 1.0f;
    return blend(previous.value, next->value, fraction);
}

glm::vec3 sampleVec3(const std::vector<Vec3Key>& keys, float time) {
    return sampleKeys(keys, time, [](const glm::vec3& from, const glm::vec3& to, float fraction) { return from + (to - from) * fraction; });
}

glm::quat sampleQuat(const std::vector<QuatKey>& keys, float time) {
    return sampleKeys(keys, time, [](const glm::quat& from, const glm::quat& to, float fraction) { return glm::normalize(glm::slerp(from, to, fraction)); });
}

glm::mat4 compose(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}

float advanceClipTime(Animator& animator, const AnimationClip& clip, float deltaTime) {
    const float duration = clip.duration();
    animator.time += deltaTime * animator.speed;
    if (!std::isfinite(animator.time)) animator.time = 0.0f;
    if (duration <= 0.0f) return 0.0f;
    float time = animator.time;
    if (clip.loop) {
        time = std::fmod(time, duration);
        if (time < 0.0f) time += duration;
        animator.time = time;
    } else if (time >= duration) {
        time = duration;
        animator.time = duration;
        animator.playing = false;
    } else if (time < 0.0f) {
        time = 0.0f;
        animator.time = 0.0f;
        animator.playing = false;
    }
    return time;
}
}

float AnimationClip::duration() const {
    float longest = 0.0f;
    if (!position.empty()) longest = std::max(longest, position.back().time);
    if (!rotation.empty()) longest = std::max(longest, rotation.back().time);
    if (!scale.empty()) longest = std::max(longest, scale.back().time);
    return longest;
}

uint32_t AnimationLibrary::add(AnimationClip clip) {
    const auto byTime = [](const auto& lhs, const auto& rhs) { return lhs.time < rhs.time; };
    std::stable_sort(clip.position.begin(), clip.position.end(), byTime);
    std::stable_sort(clip.rotation.begin(), clip.rotation.end(), byTime);
    std::stable_sort(clip.scale.begin(), clip.scale.end(), byTime);
    m_clips.push_back(std::move(clip));
    return static_cast<uint32_t>(m_clips.size() - 1);
}

const AnimationClip* AnimationLibrary::get(uint32_t id) const { return id < m_clips.size() ? &m_clips[id] : nullptr; }

uint32_t AnimationLibrary::find(const std::string& name) const {
    for (uint32_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex) {
        if (m_clips[clipIndex].name == name) return clipIndex;
    }
    return INVALID_ENTITY;
}

void updateAnimators(World& world, const AnimationLibrary& library, float dt) {
    world.view<Animator>([&](EntityHandle entity, Animator& animator) {
        if (!animator.playing) return;
        const AnimationClip* clip = library.get(animator.clip);
        if (!clip) return;
        const float time = advanceClipTime(animator, *clip, dt);

        const glm::vec3 position = clip->position.empty() ? world.getPosition(entity) : sampleVec3(clip->position, time);
        const glm::quat rotation = clip->rotation.empty() ? world.getRotation(entity) : sampleQuat(clip->rotation, time);
        const glm::vec3 scale = clip->scale.empty() ? world.getScale(entity) : sampleVec3(clip->scale, time);
        world.setLocalMatrix(entity, compose(position, rotation, scale));
    });
}

void registerAnimationComponents(World& world) {
    world.registerComponent<Animator>(
        "Animator",
        [](const Animator& animator, std::ostream& out) { out << animator.clip << ' ' << animator.time << ' ' << animator.speed << ' ' << int(animator.playing); },
        [](std::istream& in) -> std::optional<Animator> {
            Animator animator;
            int playing = 0;
            if (!(in >> animator.clip >> animator.time >> animator.speed >> playing)) return std::nullopt;
            animator.playing = playing != 0;
            return animator;
        });
}
