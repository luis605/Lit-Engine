#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include "Helpers.hpp"
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <NodeEditor/examples/application/include/application.h>
#include <NodeEditor/imgui_canvas.h>
#include <NodeEditor/imgui_node_editor.h>
#include <vector>

void DrawTextInNodeEditor(const char* label, bool isWarning) {
    ImColor labelColor =
        isWarning ? ImColor(200, 20, 20, 255) : ImColor(30, 30, 30, 255);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
    auto size = ImGui::CalcTextSize(label);

    auto padding = ImGui::GetStyle().FramePadding;
    auto spacing = ImGui::GetStyle().ItemSpacing;

    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

    auto rectMin = ImGui::GetCursorScreenPos() - padding;
    auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

    auto drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(rectMin, rectMax, labelColor, size.y * 0.15f);
    ImGui::TextUnformatted(label);
}

std::vector<ed::PinId> GetConnectedInputPins(MaterialNodeSystem& nodeSystem, ed::PinId outputPinId) {
    std::vector<ed::PinId> connectedInputsId;

    for (const auto& link : nodeSystem.m_Links) {
        if (link.StartPinID == outputPinId) {
            connectedInputsId.push_back(link.EndPinID);
        }
    }

    return connectedInputsId;
}

std::vector<Pin*> FindConnectedPins(MaterialNodeSystem& nodeSystem,
                                    const Node& materialNode,
                                    const std::vector<Link>& links) {
    std::vector<Pin*> connectedPins;
    for (const auto& pin : materialNode.Inputs) {
        for (const auto& link : links) {
            if (link.EndPinID == pin.ID) {
                // Find the corresponding pin from the start node
                Pin* connectedPin = nodeSystem.FindPin(link.StartPinID);
                if (connectedPin) {
                    connectedPins.push_back(connectedPin);
                }
            }
        }
    }
    return connectedPins;
}

int FindNodeByPinID(const std::vector<Node>& nodes, int pinID) {
    for (const auto& node : nodes) {
        for (const auto& pin : node.Outputs) {
            if (pin.ID.Get() == pinID) {
                return node.ID.Get();
            }
        }

        for (const auto& pin : node.Inputs) {
            if (pin.ID.Get() == pinID) {
                return node.ID.Get();
            }
        }
    }
    return -1;
}
