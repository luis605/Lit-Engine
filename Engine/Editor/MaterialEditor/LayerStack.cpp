/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "LayerStack.hpp"
#include "imgui.h"
#include <algorithm>

void LayerStackPanel::Render(LivingMaterial& material) {
    ImGui::Begin("Layer Stack");

    if (ImGui::Button("+")) {
        MaterialLayer newLayer;
        newLayer.m_Name = "New Layer";
        newLayer.m_IsEnabled = true;
        material.m_Layers.push_back(newLayer);
    }

    for (int i = 0; i < material.m_Layers.size(); ++i) {
        // Base layer is not removable! Make it selectable tho...
        bool is_selected = (m_SelectedLayer == i);
        ImGui::PushID(i);
        if (i == 0) {
            ImGui::Selectable(material.m_Layers[i].m_Name.c_str(), &is_selected, ImGuiSelectableFlags_Disabled);
        } else {
            if (ImGui::Selectable(material.m_Layers[i].m_Name.c_str(), is_selected)) {
                m_SelectedLayer = i;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("MATERIAL_LAYER", &i, sizeof(int));
                ImGui::Text("Dragging %s", material.m_Layers[i].m_Name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_LAYER")) {
                int dragged_idx = *(const int*)payload->Data;
                if (dragged_idx != i && dragged_idx != 0 && i != 0) { // Prevent dragging to self and reordering base layer, probably not an issue but who knows..
                    MaterialLayer dragged_layer = material.m_Layers[dragged_idx];
                    material.m_Layers.erase(material.m_Layers.begin() + dragged_idx);
                    material.m_Layers.insert(material.m_Layers.begin() + i, dragged_layer);

                    if (m_SelectedLayer == dragged_idx) {
                        m_SelectedLayer = i;
                    } else if (m_SelectedLayer == i) {
                        m_SelectedLayer = dragged_idx;
                    } else if (dragged_idx < m_SelectedLayer && i > m_SelectedLayer) {
                        m_SelectedLayer--;
                    } else if (dragged_idx > m_SelectedLayer && i < m_SelectedLayer) {
                        m_SelectedLayer++;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }

    ImGui::End();
}

int LayerStackPanel::GetSelectedLayer() const {
    return m_SelectedLayer;
}
