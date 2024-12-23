void DrawTextInNodeEditor(const char* label, bool isWarning) {
    ImColor labelColor = isWarning ? ImColor(200, 20, 20, 255)
                         : ImColor(30, 30, 30, 255);

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


std::vector<ed::PinId> GetConnectedInputPins(ed::PinId outputPinId) {
    std::vector<ed::PinId> connectedInputsId;

    for (const auto& link : materialNodeSystem.m_Links) {
        if (link.StartPinID == outputPinId) {
            connectedInputsId.push_back(link.EndPinID);
        }
    }

    return connectedInputsId;
}

std::vector<Pin*> findConnectedPins(const Node& materialNode, const std::vector<Link>& links) {
    std::vector<Pin*> connectedPins;
    for (const auto& pin : materialNode.Inputs) {
        for (const auto& link : links) {
            if (link.EndPinID == pin.ID) {
                // Find the corresponding pin from the start node
                Pin* connectedPin = materialNodeSystem.FindPin(link.StartPinID);
                if (connectedPin) {
                    connectedPins.push_back(connectedPin);
                }
            }
        }
    }
    return connectedPins;
}

int findNodeByPinID(const std::vector<Node>& nodes, int pinID) {
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
