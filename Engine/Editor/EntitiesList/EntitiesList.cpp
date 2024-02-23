#include "../../../include_all.h"
#include "../../globals.h"





void updateListViewExList(vector<Entity>& entities, vector<Light>& lights) {
    listViewExList.clear();
    objectNames.clear();
    listViewExListTypes.clear();

    for (int index = 0; index < entities.size() + lights.size(); index++) {
        string name;
        if (index < entities.size()) {
            name = entities[index].name;
            listViewExListTypes.push_back("entity");
        } else {
            name = lights_info[index - entities.size()].name;
            listViewExListTypes.push_back("light");
        }
        objectNames.push_back(name);
    }


    for (int i = 0; i < objectNames.size(); i++) {
        listViewExList.push_back((char*)objectNames[i].c_str());
    }
}

bool should_change_object_name = false;
bool showManipulateEntityPopup = false;

void ManipulateEntityPopup()
{
    if (showManipulateEntityPopup)
        ImGui::OpenPopup("Entity");

    if (ImGui::BeginPopup("Entity"))
    {
        if (ImGui::Button("Copy Entity"))
        {
            current_copy_type = CopyType_Entity;
            copiedEntity = std::make_shared<Entity>(*selected_entity);
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Delete Entity"))
        {
            entities_list_pregame.erase(std::remove(entities_list_pregame.begin(), entities_list_pregame.end(), *selected_entity), entities_list_pregame.end());
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Locate Entity"))
        {
            LocateEntity(*selected_entity);
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Rename Entity"))
        {
            should_change_object_name = true;
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Duplicate Entity"))
        {
            DuplicateEntity(*selected_entity);
            showManipulateEntityPopup = false;
        }
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            showManipulateEntityPopup = false;
        }

        ImGui::EndPopup();
    }

}


void DrawEntityTree(Entity& entity, int active, int& index, int depth = 0);
void DrawLightTree(Light& light, AdditionalLightInfo& light_info, int active, int& index);
void DrawTextElementsTree(Text& text, int active, int& index);
void DrawButtonTree(LitButton& button, int active, int& index);
void DrawCameraTree(int active, int& index);

void DrawCameraTree(int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = 0;

    if (selected_game_object_type == "camera") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    const char icon[] = ICON_FA_CAMERA;
    const char space[] = " ";
    std::string button_name = std::string(icon) + space + "Camera";

    bool isNodeOpen = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    isNodeOpen = ImGui::TreeNodeEx(button_name.c_str(), nodeFlags);
    ImGui::PopStyleColor();


    if (ImGui::IsItemClicked()) {
        active = index;
        selected_game_object_type = "camera";
    }
    index++;
    if (isNodeOpen) ImGui::TreePop();
}






void DrawEntityTree(Entity& entity, int active, int& index, int depth) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (selected_entity == &entity && selected_game_object_type == "entity") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;

        if (should_change_object_name) {
            char nameBuffer[256];
            strcpy(nameBuffer, entity.name.c_str());

            if (ImGui::InputText("##LightName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                entity.name = nameBuffer;
                should_change_object_name = false;
            }
        }
    }

    const char icon[] = ICON_FA_CUBE;
    const char space[] = " ";
    std::string entity_name = std::string(icon) + space + entity.name;

    bool isNodeOpen = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    isNodeOpen = ImGui::TreeNodeEx((void*)&entity, nodeFlags, entity_name.c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        showManipulateEntityPopup = true;
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

        ImGui::SetDragDropPayload("CHILD_ENTITY_PAYLOAD", &entity.id, sizeof(int));
        ImGui::TreeNodeEx((void*)&entity, nodeFlags | ImGuiTreeNodeFlags_Selected, entity.name.c_str());
        ImGui::PopStyleColor();

        ImGui::EndDragDropSource();
    }


    // Drag and drop target
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_LIGHT_PAYLOAD");
        if (payload) {
            // Retrieve the light ID from the payload data.
            int droppedLightID = *(const int*)payload->Data;

            auto it = std::find_if(lights.begin(), lights.end(), [droppedLightID](const Light& light) {
                return light.id == droppedLightID;
            });

            if (it != lights.end()) {
                Light* foundLight = (Light*)&*it;
                foundLight->isChild = true;
                entity.addChild(foundLight, droppedLightID);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_ENTITY_PAYLOAD");
        if (payload) {
            // Retrieve the entity ID from the payload data.
            int droppedEntityID = *(const int*)payload->Data;

            auto it = std::find_if(entities_list_pregame.begin(), entities_list_pregame.end(), [droppedEntityID](const Entity& entity) {
                return entity.id == droppedEntityID;
            });

            if (it != entities_list_pregame.end()) {
                Entity* foundEntity = (Entity*)&*it;
                foundEntity->isChild = true;
                entity.addChild(*foundEntity);

                auto it = std::find(entities_list_pregame.begin(), entities_list_pregame.end(), foundEntity);
                
                if (it != entities_list_pregame.end()) {
                    entities_list_pregame.erase(it);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked() || showManipulateEntityPopup) {
        selected_entity = &entity;
        active = index;
        selected_game_object_type = "entity";
        object_in_inspector = &entity;
    }



    if (isNodeOpen) {
        for (int childIndex = 0; childIndex < entity.children.size(); childIndex++) {
            index++;
            std::variant<Entity*, Light*, Text*, LitButton*> childVariant = entity.children[childIndex];

            if (auto* childEntity = std::get_if<Entity*>(&childVariant)) {
                // Handle the Entity type.
                DrawEntityTree(**childEntity, active, index, depth + 1);
            } else if (auto* childLight = std::get_if<Light*>(&childVariant)) {
                auto it = std::find_if(lights.begin(), lights.end(),
                    [childLight](Light light) { return light.id == (**childLight).id; });
                
                int distance;
                if (it != lights.end()) {
                    distance = std::distance(lights.begin(), it);
                } else {
                    distance = -1;
                }
                DrawLightTree(**childLight, lights_info[distance], active, index);
            } else if (auto* childText = std::get_if<Text*>(&childVariant)) {
                DrawTextElementsTree(**childText, active, index);
            } else if (auto* childButton = std::get_if<LitButton*>(&childVariant)) {
                DrawButtonTree(**childButton, active, index);
            }
        }
        ImGui::TreePop();
    }
}



void DrawLightTree(Light& light, AdditionalLightInfo& light_info, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selected_light == &light && selected_game_object_type == "light") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (should_change_object_name) {
            char nameBuffer[256];
            strcpy(nameBuffer, light_info.name.c_str());

            if (ImGui::InputText("##LightName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                light_info.name = nameBuffer;
                should_change_object_name = false;
            }
        }
    }

    const char icon[] = ICON_FA_LIGHTBULB;
    const char space[] = " ";
    std::string light_name = std::string(icon) + space + light_info.name;

    bool isNodeOpen = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    isNodeOpen = ImGui::TreeNodeEx((void*)&light, nodeFlags, light_name.c_str());
    ImGui::PopStyleColor();


    // Drag and drop source
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

        ImGui::SetDragDropPayload("CHILD_LIGHT_PAYLOAD", &light.id, sizeof(int));
        ImGui::TreeNodeEx((void*)&light, nodeFlags | ImGuiTreeNodeFlags_Selected, light_name.c_str());
        ImGui::PopStyleColor();

        ImGui::EndDragDropSource();
    }


    if (ImGui::IsItemClicked()) {
        selected_light = &light;
        active = index;
        selected_game_object_type = "light";
        object_in_inspector = &light;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}


void DrawTextElementsTree(Text& text, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selected_textElement == &text && selected_game_object_type == "text") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (should_change_object_name) {
            char nameBuffer[256];
            strcpy(nameBuffer, text.name.c_str());

            if (ImGui::InputText("##TextName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                text.name = nameBuffer;
                should_change_object_name = false;
            }
        }
    }
    const char icon[] = ICON_FA_TEXT_SLASH;
    const char space[] = " ";
    
    std::string text_name = std::string(icon) + space + text.name;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&text, nodeFlags, text_name.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        selected_textElement = &text;
        active = index;
        selected_game_object_type = "text";
        object_in_inspector = &text;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}


void DrawButtonTree(LitButton& button, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selected_button == &button && selected_game_object_type == "button") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (should_change_object_name) {
            char nameBuffer[256];
            strcpy(nameBuffer, button.name.c_str());

            if (ImGui::InputText("##ButtonName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                button.name = nameBuffer;
                should_change_object_name = false;
            }
        }
    }
    const char icon[] = ICON_FA_STOP;
    const char space[] = " ";
    
    std::string button_name = std::string(icon) + space + button.name;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&button, nodeFlags, button_name.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        selected_button = &button;
        active = index;
        selected_game_object_type = "button";
        object_in_inspector = &button;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}



void ImGuiListViewEx(vector<string>& items, int& focus, int& scroll, int& active) {
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    
    ImVec2 childSize = ImVec2(
        ImGui::GetWindowSize().x - 30,
        ImGui::GetWindowSize().y - 150);

    ImGui::BeginChild("Entities List", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered() || ImGui::IsItemHovered()) && IsKeyDown(KEY_F2))
        should_change_object_name = true;

    if (ImGui::IsWindowFocused() && IsKeyDown(KEY_ESCAPE))
        should_change_object_name = false;


    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray

    ImGui::PushItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10,10));

    int currentAmount = 0;
    int index = 0;

    ImGui::PopFont();
    ImGui::PushFont(s_Fonts["ImGui Default"]);

    DrawCameraTree(active, index);

    for (Entity& entity : entities_list_pregame) {
        DrawEntityTree(entity, active, index);
    }

    int lights_index = 0;
    for (Light& light : lights) {
        if (light.isChild) continue;
        DrawLightTree(light, lights_info[lights_index], active, index);
        lights_index++;
    }

    for (Text& text : textElements) {
        DrawTextElementsTree(text, active, index);
    }

    for (LitButton& button : lit_buttons) {
        DrawButtonTree(button, active, index);
    }

    ImGui::PopFont();
    ImGui::PushFont(s_Fonts["Default"]);

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();

    ManipulateEntityPopup();
}


void EntitiesList()
{

    ImGui::PushFont(s_Fonts["Default"]);
    ImGui::Begin((std::string(ICON_FA_BARS) + " Objects List").c_str(), NULL);
    ImGui::PopFont();

    updateListViewExList(entities_list_pregame, lights_list_pregame);
    ImGuiListViewEx(objectNames, listViewExFocus, listViewExScrollIndex, listViewExActive);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImVec2 buttonSize = ImVec2(50, 50);

    if (ImGui::ImageButton((ImTextureID)&run_texture, buttonSize)) {
        entities_list.assign(entities_list_pregame.begin(), entities_list_pregame.end());
        
        // DisableCursor();
        physics.backup();

        InitGameCamera();
        in_game_preview = true;
    }

    ImGui::SameLine();

    if ((ImGui::ImageButton((ImTextureID)&pause_texture, buttonSize)) && in_game_preview || IsKeyDown(KEY_ESCAPE)) {
        EnableCursor();
        
        in_game_preview = false;
        first_time_gameplay = true;

        physics.unBackup();
        
        for (Entity& entity : entities_list_pregame)
        {
            entity.resetPhysics();
        }

        for (Entity& entity : entities_list)
        {
            entity.resetPhysics();
        }
    }


    if (IsKeyPressed(KEY_F1))
    {
        openAboutPage();
    }

    if (IsKeyPressed(KEY_F2) && !in_game_preview && !ImGui::IsWindowFocused)
    {
        openManualPage();
    }


    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    ImGui::End();
}