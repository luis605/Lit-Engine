#ifndef ENTITIESLIST_H
#define ENTITIESLIST_H

void DrawEntityTree(Entity& entity);
void DrawLightTree(LightStruct& lightStruct);
void DrawTextElementsTree(Text& text);
void DrawButtonTree(LitButton& button);
void DrawCameraTree();
void updateListViewExList(std::vector<Entity>& entities, std::vector<Light>& lights);
void ManipulateEntityPopup();

bool draggingChildObject = false;
bool shouldChangeObjectName = false;
bool showManipulateEntityPopup = false;

int entitiesListTreeNodeIndex;

#endif // ENTITIESLIST_H