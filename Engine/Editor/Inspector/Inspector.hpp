/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef INSPECTOR_H
#define INSPECTOR_H

#include <filesystem>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>

namespace fs = std::filesystem;

extern Vector3 selectedEntityScale;
extern Vector3 selectedEntityPosition;

extern bool showAlbedoTexture;
extern bool showNormalTexture;
extern bool showRoughnessTexture;
extern bool showAOTexture;
extern bool showHeightTexture;
extern bool showMetallicTexture;
extern bool showEmissiveTexture;
extern bool showSkyboxTexture;

extern bool EntityRotationXInputModel;
extern bool EntityRotationYInputModel;
extern bool EntityRotationZInputModel;

extern bool ButtonRoundnessActiveInputMode;

extern bool AttenuationActiveInputMode;
extern bool IntensityActiveInputMode;
extern bool SpecularStrenghtActiveInputMode;
extern bool RadiusActiveInputMode;
extern bool InnerCutoffActiveInputMode;
extern bool OuterCutoffActiveInputMode;

extern bool FontSizeActiveInputMode;
extern bool TextSpacingActiveInputMode;
extern bool TextBackgroundRoundinessActiveInputMode;
extern bool TextPaddingActiveInputMode;

extern bool WorldGravityXInputMode;
extern bool WorldGravityYInputMode;
extern bool WorldGravityZInputMode;

void Inspector();
void EntityInspector();
void LightInspector();
void TextInspector();
void ButtonInspector();
void CameraInspector();
void WorldInspector();
void MaterialInspector(ChildMaterial& material);

#endif // INSPECTOR_H