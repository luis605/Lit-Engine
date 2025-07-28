/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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

    for (const auto& [linkId, link] : nodeSystem.m_links) {
        if (link.m_startPinId == outputPinId) {
            connectedInputsId.push_back(link.m_endPinId);
        }
    }

    return connectedInputsId;
}

std::vector<Pin*> FindConnectedPins(MaterialNodeSystem& nodeSystem,
                                    const Node& materialNode,
                                    const std::vector<Link>& links) {
    std::vector<Pin*> connectedPins;
    for (const auto& pinId : materialNode.m_inputs) {
        for (const auto& link : links) {
            if (link.m_endPinId == pinId) {
                // Find the corresponding pin from the start node
                Pin* connectedPin = nodeSystem.FindPin(link.m_startPinId);
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
        for (const auto& pinId : node.m_outputs) {
            if (pinId.Get() == pinID) {
                return node.m_id.Get();
            }
        }

        for (const auto& pinId : node.m_inputs) {
            if (pinId.Get() == pinID) {
                return node.m_id.Get();
            }
        }
    }
    return -1;
}
