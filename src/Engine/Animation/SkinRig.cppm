module;

#include <cstdint>
#include <vector>

export module Engine.SkinRig;

import Engine.glm;
import Engine.Skin;
import Engine.Rig;
import Engine.Face;

// Bridges DynamicMesh (renderer-agnostic skin data) to the skeletal animation and blendshape modules.

export [[nodiscard]] Skeleton skeletonFromSkin(const DynamicMesh& mesh);
export [[nodiscard]] SkeletalClip clipFromSkin(const SkinClip& clip);
// Bone palette = jointModel * inverseBind for every skin joint of `mesh`, ready for Renderer::setDynamicPose.
export void computeSkinPalette(const Skeleton& skeleton, const Pose& pose, const DynamicMesh& mesh, std::vector<glm::mat4>& palette);
// Maps ARKit-52 weights to the morph targets of `mesh` by name (targets without an ARKit name get 0).
export void morphWeightsFromBlendshapes(const DynamicMesh& mesh, const BlendshapeWeights& weights, std::vector<float>& out);
// Procedural clips for makeProceduralCharacter's skeleton: "walk" and "wave" (looping).
export [[nodiscard]] SkeletalClip makeWalkClip(const Skeleton& skeleton, float duration = 1.0f);
export [[nodiscard]] SkeletalClip makeWaveClip(const Skeleton& skeleton, float duration = 1.6f);
// A looping blendshape clip: talking jaw plus smile and blinks.
export [[nodiscard]] BlendshapeClip makeTalkClip(float duration = 2.0f);
