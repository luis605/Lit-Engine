/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef OBJECTS_LIST_H
#define OBJECTS_LIST_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Entity.hpp>
#include <Engine/GUI/Button/Button.hpp>
#include <Engine/GUI/Text/Text.hpp>
#include <Engine/Lighting/lights.hpp>
#include <string>
#include <vector>

void DrawEntityTree(Entity& entity);
void DrawLightTree(LightStruct& lightStruct);
void DrawTextElementsTree(Text& text);
void DrawButtonTree(LitButton& button);
void DrawCameraTree();
void updateListViewExList(std::vector<Entity>& entities,
                          std::vector<Light>& lights);
void ManipulateEntityPopup();
bool DrawNodeTree(const char* icon, const std::string& name,
                  ImGuiTreeNodeFlags& flags, void* ptr, const bool& isSelected,
                  const std::function<void()>& callback,
                  bool rightClicked = false);
bool DrawTreeNodeWithRename(const char* icon, std::string& name, void* ptr,
                            ImGuiTreeNodeFlags& flags, const bool& isSelected,
                            const std::function<void()>& callback,
                            bool rightClicked = false);
void UnchildObjects(ImVec2 childSize);
void ImGuiListViewEx();
void OpenWebpages();
void EntitiesList();

extern bool draggingChildObject;
extern bool shouldChangeObjectName;
extern bool showManipulateEntityPopup;
extern int entitiesListTreeNodeIndex;

#endif // OBJECTS_LIST_H