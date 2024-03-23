#ifndef INSPECTOR_H
#define INSPECTOR_H

std::string selectedEntityName                    = "";
Vector3 selectedEntityScale                  = {1, 1, 1};
Vector3 selectedEntityPosition               = {0, 0, 0};
Vector3 selectedEntityRelativePosition       = {0, 0, 0};
Vector3 selectedEntityRotation               = {0, 0, 0};
Color selectedEntityColor                    = RED;
ImVec4 selectedEntityColorImGui              = { 0, 0, 0, 0 };
std::string selectedEntityScriptPath              = "";
Texture2D entityTexture;
int selectedEntityScriptPathIndex            = 0;
int selectedEntityModelPathIndex             = 0;
bool doPhysics                               = false;

bool showTexture                             = false;
bool showNormalTexture                       = false;
bool showRoughnessTexture                    = false;
bool showAOTexture                           = false;
bool showSkyboxTexture                       = false;

bool EntityRotationXInputModel               = false;
bool EntityRotationYInputModel               = false;
bool EntityRotationZInputModel               = false;

bool ButtonPaddingActiveInputMode            = false;
bool ButtonRoundnessActiveInputMode          = false;

bool AttenuationActiveInputMode              = false;
bool IntensityActiveInputMode                = false;
bool SpecularStrenghtActiveInputMode         = false;
bool CutoffActiveInputMode                   = false;

bool FontSizeActiveInputMode                 = false;
bool TextSpacingActiveInputMode              = false;
bool TextBackgroundRoundinessActiveInputMode = false;
bool TextPaddingActiveInputMode              = false;

bool WorldGravityXInputMode                  = false;
bool WorldGravityYInputMode                  = false;
bool WorldGravityZInputMode                  = false;

#include "Inspector.Material.cpp"
#include "Inspector.Entity.cpp"
#include "Inspector.Light.cpp"
#include "Inspector.Text.cpp"
#include "Inspector.Button.cpp"
#include "Inspector.World.cpp"
#include "Inspector.Camera.cpp"

#endif // INSPECTOR_H