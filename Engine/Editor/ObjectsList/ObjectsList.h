#ifndef OBJECTS_LIST_H
#define OBJECTS_LIST_H

void DrawEntityTree(Entity& entity);
void DrawLightTree(LightStruct& lightStruct);
void DrawTextElementsTree(Text& text);
void DrawButtonTree(LitButton& button);
void DrawCameraTree();
void updateListViewExList(std::vector<Entity>& entities, std::vector<Light>& lights);
void ManipulateEntityPopup();
bool DrawNodeTree(const char* icon, const std::string& name, ImGuiTreeNodeFlags flags, void* ptr, bool isSelected, const std::function<void()>& callback, bool* rightClicked = nullptr);
bool DrawTreeNodeWithRename(const char* icon, std::string& name, void* ptr, ImGuiTreeNodeFlags flags, bool isSelected, const std::function<void()>& callback, bool* rightClicked = nullptr);
void UnchildObjects(ImVec2 childSize);
void ImGuiListViewEx();
void OpenWebpages();
void EntitiesList();

bool draggingChildObject = false;
bool shouldChangeObjectName = false;
bool showManipulateEntityPopup = false;
int entitiesListTreeNodeIndex;

#endif // OBJECTS_LIST_H