module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

export module Engine.Rig;

import Engine.glm;
import Engine.Animation;
import Engine.World;
import Engine.Render.entity;

export struct Joint {
    std::string name;
    int parent = -1;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

export struct Skeleton {
    std::vector<Joint> joints;
    int add(const std::string& name, int parent, const glm::vec3& position, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), const glm::vec3& scale = glm::vec3(1.0f));
    [[nodiscard]] int find(const std::string& name) const;
    [[nodiscard]] size_t size() const { return joints.size(); }
};

export struct JointPose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

export struct Pose {
    std::vector<JointPose> joints;
    static Pose bind(const Skeleton& skeleton);
    void assignBind(const Skeleton& skeleton);
};

export struct ModelPose {
    std::vector<glm::vec3> position;
    std::vector<glm::quat> rotation;
};

export struct JointTrack {
    int joint = -1;
    std::vector<Vec3Key> position;
    std::vector<QuatKey> rotation;
    std::vector<Vec3Key> scale;
};

export struct SkeletalClip {
    std::string name;
    float duration = 0.0f;
    bool loop = true;
    std::vector<JointTrack> tracks;
};

export class SkeletalLibrary {
  public:
    uint32_t add(SkeletalClip clip);
    [[nodiscard]] const SkeletalClip* get(uint32_t id) const { return id < m_clips.size() ? &m_clips[id] : nullptr; }
    [[nodiscard]] int find(const std::string& name) const;
    [[nodiscard]] size_t size() const { return m_clips.size(); }

  private:
    std::vector<SkeletalClip> m_clips;
};

export void sampleSkeletalClip(const Skeleton& skeleton, const SkeletalClip& clip, float time, Pose& out);
export void blendPoses(const Pose& from, const Pose& to, float weight, Pose& out);
export void blendPosesMasked(const Pose& from, const Pose& to, const std::vector<float>& mask, float weight, Pose& out);
export void addPose(Pose& base, const Pose& layer, const Pose& reference, float weight);
export [[nodiscard]] std::vector<float> subtreeMask(const Skeleton& skeleton, int rootJoint, bool includeRoot = true);
export [[nodiscard]] ModelPose computeModelPose(const Skeleton& skeleton, const Pose& pose);
export void computeModelPose(const Skeleton& skeleton, const Pose& pose, ModelPose& out);
export [[nodiscard]] std::vector<glm::mat4> computeModelMatrices(const Skeleton& skeleton, const Pose& pose);
export void computeModelMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& out);
export void applyPoseToEntities(World& world, const std::vector<EntityHandle>& jointEntities, const Pose& pose);

export struct BlendSample {
    float x = 0.0f;
    uint32_t clip = 0;
};

export class SkeletalAnimator {
  public:
    SkeletalAnimator(const Skeleton& skeleton, const SkeletalLibrary& library) : m_skeleton(&skeleton), m_library(&library) { m_pose.assignBind(skeleton); }

    void play(uint32_t clip, float fadeSeconds = 0.2f);
    void playBlendSpace(std::vector<BlendSample> samples, float parameter, float fadeSeconds = 0.2f);
    void setBlendParameter(float parameter) { m_parameter = parameter; }
    void setOverlay(uint32_t clip, std::vector<float> mask, float weight, bool additive);
    void clearOverlay() { m_overlayActive = false; }
    void setSpeed(float speed) { m_speed = speed; }
    void update(float dt);
    [[nodiscard]] const Pose& pose() const { return m_pose; }
    [[nodiscard]] float fadeRemaining() const { return m_fadeLeft; }
    [[nodiscard]] float time() const { return m_current.time; }

  private:
    struct Source {
        bool valid = false;
        uint32_t clip = 0;
        std::vector<BlendSample> space;
        float time = 0.0f;
    };
    void evaluate(const Source& source, Pose& out);
    void evaluateBlendSpace(const Source& source, Pose& out);
    void advance(Source& source, float dt) const;
    void applyFade(float dt, Pose& current);
    void applyOverlay(Pose& current);

    const Skeleton* m_skeleton;
    const SkeletalLibrary* m_library;
    Source m_current;
    Source m_previous;
    float m_fadeLeft = 0.0f;
    float m_fadeTotal = 0.0f;
    float m_speed = 1.0f;
    float m_parameter = 0.0f;
    Pose m_pose;
    Pose m_scratchPrevious;
    Pose m_scratchOverlay;
    Pose m_scratchReference;
    Pose m_scratchLower;
    Pose m_scratchHigher;
    bool m_overlayActive = false;
    Source m_overlay;
    std::vector<float> m_overlayMask;
    float m_overlayWeight = 1.0f;
    bool m_overlayAdditive = false;
};

export struct TwoBoneChain {
    int root = -1;
    int mid = -1;
    int tip = -1;
};

export bool solveTwoBone(const Skeleton& skeleton, Pose& pose, const TwoBoneChain& chain, const glm::vec3& targetModel, const std::optional<glm::vec3>& poleModel = std::nullopt, float weight = 1.0f);

export struct LookAtJoint {
    int joint = -1;
    float weight = 1.0f;
};

export void solveLookAt(const Skeleton& skeleton, Pose& pose, const std::vector<LookAtJoint>& chain, const glm::vec3& forwardAxis, const glm::vec3& targetModel, float maxAngleRadians = 3.14159265f, float weight = 1.0f);

export struct GroundSample {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

export using GroundFn = std::function<std::optional<GroundSample>(const glm::vec3& worldPosition)>;

export struct FootIkLeg {
    TwoBoneChain chain;
    float ankleHeight = 0.1f;
};

export struct FootIkSettings {
    int pelvis = -1;
    float maxPelvisDrop = 0.5f;
    float weight = 1.0f;
};

export struct FootIkResult {
    float pelvisOffset = 0.0f;
    std::vector<uint8_t> planted;
    std::vector<float> footError;
};

export FootIkResult solveFootIk(const Skeleton& skeleton, Pose& pose, const std::vector<FootIkLeg>& legs, const FootIkSettings& settings, const glm::mat4& characterToWorld, const GroundFn& ground);

namespace RigDetail {
glm::quat rotationBetween(const glm::vec3& from, const glm::vec3& to);
glm::quat nlerp(const glm::quat& from, const glm::quat& to, float fraction);
glm::quat conjugate(const glm::quat& rotation);
glm::quat limitAngle(const glm::quat& rotation, float maxAngle);
bool validJoint(const Skeleton& skeleton, int joint);
int parentOf(const Skeleton& skeleton, size_t jointIndex);
bool poseCovers(const Skeleton& skeleton, const Pose& pose);
bool finiteVec(const glm::vec3& value);
void setModelRotation(const Skeleton& skeleton, Pose& pose, const ModelPose& model, int joint, const glm::quat& wanted);
glm::vec3 transformPoint(const glm::mat4& matrix, const glm::vec3& point);
}
