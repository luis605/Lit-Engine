#ifndef INSPECTOR_H
#define INSPECTOR_H

string selected_entity_name = "";
Vector3 selected_entity_scale = {1, 1, 1};
Vector3 selected_entity_position = {0, 0, 0};
Vector3 selected_entity_relative_position = {0, 0, 0};
Vector3 selected_entity_rotation = {0, 0, 0};
Color selected_entity_color = RED;
ImVec4 selected_entity_colorImGui = { 0, 0, 0, 0 };
string selected_entity_script_path;
Texture2D entity_texture;
int selected_entity_script_path_index;
int selected_entity_model_path_index;
bool do_physics = false;

bool show_texture = false;
bool show_normal_texture = false;


bool AttenuationActiveInputMode = false;
bool IntensityActiveInputMode = false;
bool SpecularStrenghtActiveInputMode = false;
bool CutoffActiveInputMode = false;

#endif // INSPECTOR_H