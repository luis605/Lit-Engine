#include "../../include_all.h"
#include "../../globals.h"



void updateListViewExList(vector<Entity>& entities, vector<Light>& lights) {
    listViewExList.clear();
    objectNames.clear();

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

    
    // Resize listViewExList to match the size of objectNames
    listViewExList.reserve(100000000);
    listViewExList.resize(listViewExList.size()+1);

    // Set the values of listViewExList to the character pointers to the names in objectNames
    for (int i = 0; i < objectNames.size(); i++) {
        listViewExList[i] = objectNames[i].c_str();
    }
}













bool should_change_object_name = false;

void DrawEntityTree(Entity& entity, int active, int& index, int depth = 0) {

    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    
    if (selected_entity == &entity && selected_game_object_type == "entity") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;

        if (should_change_object_name) {
            char nameBuffer[256];
            strcpy(nameBuffer, entity.name.c_str());

            if (ImGui::InputText("##LightName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                entity.name = nameBuffer; // Update the 'name' member with the edited value
                should_change_object_name = false; // Set this flag to false to stop editing mode
            }
        }
    }

    const char icon[] = ICON_FA_CUBE;
    const char space[] = " ";

    std::string entity_name = std::string(icon) + space + entity.name;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&entity, nodeFlags, entity_name.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        selected_entity = &entity;
        active = index;
        selected_game_object_type = "entity";
        object_in_inspector = &entity;
        std::cout << "index: " << index << std::endl;
    }

    if (isNodeOpen) {
        for (int childIndex = 0; childIndex < entity.children.size(); childIndex++) {
            index++;
            std::variant<Entity*, Light*, Text*, LitButton*> childVariant = entity.children[childIndex];
            
            if (auto* childEntity = std::get_if<Entity*>(&childVariant)) {
                // Handle the Entity type.
                DrawEntityTree(**childEntity, active, index, depth + 1);
            } else if (auto* childLight = std::get_if<Light*>(&childVariant)) {
                // Handle the Light type.
                // Do something with *childLight.
            } else if (auto* childText = std::get_if<Text*>(&childVariant)) {
                // Handle the Text type.
                // Do something with *childText.
            } else if (auto* childButton = std::get_if<LitButton*>(&childVariant)) {
                // Handle the LitButton type.
                // Do something with *childButton.
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
                light_info.name = nameBuffer; // Update the 'name' member with the edited value
                should_change_object_name = false; // Set this flag to false to stop editing mode
            }
        }
    }
    const char icon[] = ICON_FA_LIGHTBULB;
    const char space[] = " ";
    
    std::string light_name = std::string(icon) + space + light_info.name;


    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&light, nodeFlags, light_name.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        selected_light = &light;
        active = index;
        selected_game_object_type = "light";
        object_in_inspector = &light;
        std::cout << "index: " << index << std::endl;
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
        std::cout << "index: " << index << std::endl;
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
        std::cout << "index: " << index << std::endl;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}







void ImGuiListViewEx(vector<string>& items, int& focus, int& scroll, int& active) {
    ImGui::SetNextWindowSize(ImVec2(400, 200)); // set the container size

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImVec2 childSize = ImVec2(.2 * screen.x, .7 * screen.y);
    ImGui::BeginChild("Entities List", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

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

    for (Entity& entity : entities_list_pregame) {
        DrawEntityTree(entity, active, index);
    }

    int lights_index = 0;
    for (Light& light : lights) {
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
}


void EntitiesList()
{
    const char* windowName = "Scene Objects List Window";
    ImGui::Begin(windowName, NULL);

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) && IsKeyDown(KEY_F2))
        should_change_object_name = true;

    if (ImGui::IsWindowFocused(windowName) && IsKeyDown(KEY_ESCAPE))
        should_change_object_name = false;


    updateListViewExList(entities_list_pregame, lights_list_pregame);
    ImGuiListViewEx(objectNames, listViewExFocus, listViewExScrollIndex, listViewExActive);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray

    bool add_entity = ImGui::Button("Add Entity", ImVec2(120,40));
    if (add_entity)
    {
        sprintf(name, "Entity Name");
        canAddEntity = true;
    }

    ImGui::SameLine();

    bool add_light = ImGui::Button("Add Light", ImVec2(120,40));
    if (add_light)
    {
        canAddLight = true;
        NewLight({4, 4, 4}, WHITE);
        UpdateLightsBuffer();
    }


    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));

    if (ImGui::ImageButton((ImTextureID)&run_texture, ImVec2(60, 60)))
    {
        cout << "Running Game" << endl;
        entities_list.assign(entities_list_pregame.begin(), entities_list_pregame.end());
        InitGameCamera();
        in_game_preview = true;
    }

    ImGui::SameLine();

    if (ImGui::ImageButton((ImTextureID)&pause_texture, ImVec2(60, 60)))
    {
        cout << "Stopping Game" << endl;
        in_game_preview = false;
        first_time_gameplay = true;
        CleanScriptThreads(scripts_thread_vector);
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    if (!entities_list_pregame.empty() || !lights_list_pregame.empty()) {
        // if (selected_game_object_type == "entity")
        //     object_in_inspector = &entities_list_pregame[listViewExActive];
        // else
        //     object_in_inspector = &lights_list_pregame[listViewExActive];
    } else {
        static Entity default_entity;
        object_in_inspector = &default_entity;
    }

    ImGui::End();
}

