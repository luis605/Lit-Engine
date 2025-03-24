#ifndef INSPECTOR_H
#define INSPECTOR_H

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
void MaterialInspector(SurfaceMaterial* surfaceMaterial = nullptr,
                       fs::path path = "");

#endif // INSPECTOR_H