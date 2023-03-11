#include "../../include_all.h"
#include "../../globals.h"




void updateListViewExList(vector<Entity>& entities) {
    // Clear the current values of listViewExList
    listViewExList.clear();

    // Clear the current values of entityNames
    entityNames.clear();

    // Store the names of the entities in entityNames
    for (int i = 0; i < entities.size(); i++) {
        entityNames.push_back(entities[i].getName());
    }

    
    // Resize listViewExList to match the size of entityNames
    listViewExList.reserve(100000000);
    listViewExList.resize(listViewExList.size()+1);

    // Set the values of listViewExList to the character pointers to the names in entityNames
    for (int i = 0; i < entityNames.size(); i++) {
        listViewExList[i] = entityNames[i].c_str();
    }
}




void AddEntity(void)
{
    // Define the layout of the popup
    const int POPUP_WIDTH = 600;
    const int POPUP_HEIGHT = 650;

    // Calculate the position of the popup
    int popupX = GetScreenWidth() / 4.5;
    int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

    if (create) {
        // Conversion imgui color to raylib color
        Color entity_color_raylib = (Color){ (unsigned char)(color.x*255), (unsigned char)(color.y*255), (unsigned char)(color.z*255), (unsigned char)(color.w*255) };

        // Create Entity
        Entity entity_create;
        entity_create.setColor(entity_color_raylib);
        entity_create.setScale(Vector3 { scale, scale, scale, });
        entity_create.setName(name);
        entity_create.setModel("assets/models/tree.obj");
        entities_list_pregame.push_back(entity_create);
        
        create = false;
        canAddEntity = false;

        std::cout << "\n-----------------------\nEntities:\n---------\n";
        for (int i = 0; i < entityNames.size(); i++) {
            std::cout << listViewExList[i] << std::endl;
        }
        
    }
    else if (canAddEntity)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.6f, 0.6f, 0.6f, 0.6f)); // light gray

        ImGui::Begin("Entities");

        ImVec2 size = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 50.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

        ImGui::SetCursorPosX(size.x / 2 - 250);
        ImGui::SetCursorPosY(size.y / 4);
        ImGui::Button("Entity Add Menu", ImVec2(500,100));

        ImGui::PopStyleColor(4);

        /* Scale Slider */
        ImGui::Text("Scale: ");
        ImGui::SameLine();
        ImGui::SliderFloat(" ", &scale, 0.1f, 100.0f);

        /* Name Input */
        ImGui::InputText("##text_input_box", name, sizeof(name));
        
        /* Color Picker */
        ImGui::Text("Color: ");
        ImGui::SameLine();
        ImGui::ColorEdit4(" ", (float*)&color, ImGuiColorEditFlags_NoInputs);

        /* Create BTN */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14, 0.37, 0.15, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18, 0.48, 0.19, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        ImGui::SetCursorPosX(size.x / 2);
        ImGui::SetCursorPosY(size.y / 1.1);
        bool create_entity_btn = ImGui::Button("Create", ImVec2(200,50));
        if (create_entity_btn)
        {
            canAddEntity = false;
            create = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::End();
    }
}


int ImGuiListViewEx(std::vector<std::string>& items, int& focus, int& scroll, int& active) {
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
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10,10)); // adding padding to the buttons

    // Buttons
    for (int i = 0; i < items.size(); i++)
    {
        if (i == active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.9f)); // dark gray
        
        int old_active = active;
        std::string entity_in_inspector_script_path = items[i] + "##" + std::to_string(i);
        if (ImGui::Button(entity_in_inspector_script_path.c_str(), ImVec2(120,40))) {
            focus = i;
            active = i;
        }

        if (i == old_active) ImGui::PopStyleColor();
        old_active = active;
        

    }

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    return active;
}



#include <iostream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>





// Widgets
void EntitiesList()
{
/* 
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
 */



    updateListViewExList(entities_list_pregame);

    // Translate the rectangles coordinates to ImGui coordinates
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    Rectangle rec = { pos.x, pos.y, size.x, size.y };

    listViewExActive = ImGuiListViewEx(entityNames, listViewExFocus, listViewExScrollIndex, listViewExActive);


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

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();


    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
    if (ImGui::ImageButton((ImTextureID)&run_texture, ImVec2(60, 60)))
    {
        std::cout << "Running Game" << std::endl;
        entities_list.assign(entities_list_pregame.begin(), entities_list_pregame.end()); // Copy the engine's list of entities to the ingame entities list
        InitGameCamera();
        in_game_preview = true;
    }

    ImGui::SameLine();
    
    if (ImGui::ImageButton((ImTextureID)&pause_texture, ImVec2(60, 60)))
    {
        std::cout << "Stopping Game" << std::endl;
        in_game_preview = false;
    }

    
    ImGui::PopStyleVar();


    if (!entities_list_pregame.empty()) entity_in_inspector = &entities_list_pregame[listViewExActive];
    else entity_in_inspector = &Entity();
    

}

