module;

#include <cstdint>
#include <string>
#include <vector>

export module Engine.Animation;

import Engine.Render.entity;
import Engine.World;
import Engine.glm;

export struct Vec3Key {
    float time = 0.0f;
    glm::vec3 value{0.0f};
};

export struct QuatKey {
    float time = 0.0f;
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

export struct AnimationClip {
    std::string name;
    std::vector<Vec3Key> position;
    std::vector<QuatKey> rotation;
    std::vector<Vec3Key> scale;
    bool loop = true;

    [[nodiscard]] float duration() const;
};

export struct Animator {
    uint32_t clip = 0;
    float time = 0.0f;
    float speed = 1.0f;
    bool playing = true;
};

export class AnimationLibrary {
  public:
    uint32_t add(AnimationClip clip);
    [[nodiscard]] const AnimationClip* get(uint32_t id) const;
    [[nodiscard]] uint32_t find(const std::string& name) const;
    [[nodiscard]] size_t size() const { return m_clips.size(); }

  private:
    std::vector<AnimationClip> m_clips;
};

export void updateAnimators(World& world, const AnimationLibrary& library, float dt);
export void registerAnimationComponents(World& world);
