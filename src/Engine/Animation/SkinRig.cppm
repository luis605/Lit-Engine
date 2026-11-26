module;

#include <cstdint>
#include <vector>

export module Engine.SkinRig;

import Engine.glm;
import Engine.Skin;
import Engine.Rig;
import Engine.Face;

export [[nodiscard]] Skeleton skeletonFromSkin(const DynamicMesh& mesh);
export [[nodiscard]] SkeletalClip clipFromSkin(const SkinClip& clip);
export void computeSkinPalette(const Skeleton& skeleton, const Pose& pose, const DynamicMesh& mesh, std::vector<glm::mat4>& palette);
export void morphWeightsFromBlendshapes(const DynamicMesh& mesh, const BlendshapeWeights& weights, std::vector<float>& out);
export [[nodiscard]] SkeletalClip makeWalkClip(const Skeleton& skeleton, float duration = 1.0f);
export [[nodiscard]] SkeletalClip makeWaveClip(const Skeleton& skeleton, float duration = 1.6f);
export [[nodiscard]] BlendshapeClip makeTalkClip(float duration = 2.0f);
