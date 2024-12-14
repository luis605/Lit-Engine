#include "MaterialNodeEditor.h"
#include "Drawing.cpp"
#include "Utilities.cpp"

static inline ImRect ImGui_GetItemRect() {
    return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

int MaterialNodeSystem::GetNextId() {
    return m_NextId++;
}

ed::LinkId MaterialNodeSystem::GetNextLinkId() {
    return ed::LinkId(GetNextId());
}

Node* MaterialNodeSystem::FindNode(ed::NodeId id) {
    auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [&](const Node& node) {
        return node.ID == id;
    });

    return it != m_Nodes.end() ? &(*it) : nullptr;
}

Link* MaterialNodeSystem::FindLink(ed::LinkId id) {
    for (auto& link : m_Links)
        if (link.ID == id)
            return &link;

    return nullptr;
}

Pin* MaterialNodeSystem::FindPin(ed::PinId id) {
    if (!id)
        return nullptr;

    for (auto& node : m_Nodes) {
        for (auto& pin : node.Inputs)
            if (pin.ID == id)
                return &pin;

        for (auto& pin : node.Outputs)
            if (pin.ID == id)
                return &pin;
    }

    return nullptr;
}

bool MaterialNodeSystem::IsPinLinked(ed::PinId id) {
    if (!id)
        return false;

    for (auto& link : m_Links)
        if (link.StartPinID == id || link.EndPinID == id)
            return true;

    return false;
}

bool MaterialNodeSystem::CanCreateLink(Pin* a, Pin* b) {
    if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node)
        return false;

    return true;
}

void MaterialNodeSystem::BuildNode(Node* node) {
    for (auto& input : node->Inputs)
    {
        input.Node = node;
        input.Kind = PinKind::Input;
    }

    for (auto& output : node->Outputs)
    {
        output.Node = node;
        output.Kind = PinKind::Output;
    }
}

void MaterialNodeSystem::BuildNodes() {
    for (auto& node : m_Nodes)
        BuildNode(&node);
}

void DrawNodeTitle(const std::string& title, const float& width, const ImColor& bgColor, const ed::NodeId& id) {
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    constexpr float height = 30.0f;

    ImVec2 rectMin = cursorPos;
    ImVec2 rectMax = ImVec2(rectMin.x + width, rectMin.y + height);

    const float uvWidth = std::min(width / noiseTexture.width, 1.0f);
    const float uvHeight = std::min(height / noiseTexture.height, 1.0f);

    const ImVec2 uvMin(0.0f, 0.0f);
    const ImVec2 uvMax(uvWidth, uvHeight);

    ImGui::GetWindowDrawList()->AddImageRounded(
        (ImTextureID)&noiseTexture,
        rectMin, rectMax,
        uvMin, uvMax,
        ImGui::ColorConvertFloat4ToU32(bgColor),
        5.0f, ImDrawFlags_RoundCornersTop
    );

    ImVec2 textSize = ImGui::CalcTextSize(title.c_str());
    ImVec2 textPos(
        rectMin.x + (width  - textSize.x) * 0.5f,
        rectMin.y + (height - textSize.y) * 0.5f
    );

    ImGui::SetCursorScreenPos(textPos);
    ImGui::Text(title.c_str());

    const float halfBorderWidth = ed::GetStyle().NodeBorderWidth * 0.5f;

    constexpr ImColor borderColor(100, 100, 100, 250);

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(rectMin.x, rectMax.y),
        rectMax,
        borderColor,
        1.0f
    );

    ImGui::SetCursorScreenPos(cursorPos);
    std::string invisibleButtonText = "NodeTitleInvisibleButton_" + id.Get();
    ImGui::InvisibleButton(invisibleButtonText.c_str(), ImVec2(width, 0.1)); // Ensures the node has the correct width

    ImGui::SetCursorScreenPos(ImVec2(rectMin.x, rectMax.y));
}

void DrawPinIcon(const ImVec2& size, MaterialDrawing::IconType type, bool filled, const ImVec4& color, const ImVec4& innerColor) {
    if (ImGui::IsRectVisible(size)) {
        auto cursorPos = ImGui::GetCursorScreenPos();
        auto drawList  = ImGui::GetWindowDrawList();
        MaterialDrawing::DrawIcon(drawList, cursorPos, cursorPos + size, type, filled, ImColor(color), ImColor(innerColor));
    }

    ImGui::Dummy(size);
}

void MaterialNodeSystem::DrawNodeMiddleSection(Node& node, const ImVec2& cursorStartPos) {
    if (node.type == NodeType::Color) {
        ColorNode* nodeData = GetNodeData<ColorNode>(node).value_or(nullptr);

        if (nodeData) {
            ImGuiColorEditFlags colorEditFlags =
                ImGuiColorEditFlags_NoTooltip |
                ImGuiColorEditFlags_NoLabel |
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_PickerHueBar;

            ImGui::SetCursorPosX(cursorStartPos.x + padding);
            ImGui::SetNextItemWidth(200.0f);
            ImGui::ColorPicker4("Color", (float*)&nodeData->color, colorEditFlags);
            node.Outputs.at(0).Value = (void*)&nodeData->color;
        }
    } else if (node.type == NodeType::Texture) {
        TextureNode* nodeData = GetNodeData<TextureNode>(node).value_or(nullptr);

        if (nodeData) {
            const std::string imageButtonId = "##textureButtonID_" + node.ID.Get();

            ImGui::SetCursorPosX(cursorStartPos.x + padding);
            ImGui::ImageButton(imageButtonId.c_str(), (ImTextureID)&nodeData->texture.getTexture2D(), ImVec2(140, 140));

            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

                if (payload) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int payload_n = *(const int*)payload->Data;

                    nodeData->texture = dirPath / fileStruct[payload_n].name;
                }

                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            const std::string emptyButtonName = std::string("x##CleanupTexture" + node.Name + "EmptyButton");
            if (ImGui::Button(emptyButtonName.c_str(), ImVec2(25, 25))) {
                nodeData->texture.cleanup();
            }

            node.Outputs.at(0).Value = (void*)&nodeData->texture;
        }
    } else if (node.type == NodeType::Slider) {
        SliderNode* nodeData = GetNodeData<SliderNode>(node).value_or(nullptr);

        if (nodeData) {
            ImGui::SetCursorPosX(cursorStartPos.x + padding);
            ImGui::SetNextItemWidth(370.0f);
            ImGui::PushID(node.ID.Get());

            if (nodeData->onlyInt) {
                int intValue = static_cast<int>(nodeData->value);
                if (ImGui::SliderInt("", &intValue, SLIDER_MIN, SLIDER_MAX)) {
                    nodeData->value = static_cast<float>(intValue);
                }
            } else {
                ImGui::SliderFloat("", &nodeData->value, SLIDER_MIN, SLIDER_MAX);
            }

            node.Outputs.at(0).Value = new float(nodeData->value);

            ImGui::PopID();

            ImGui::SetCursorPosX(cursorStartPos.x + padding);
            ImGui::Text("Only Int: ");
            ImGui::SameLine();
            ImGui::PushID(std::string("OnlyInt_" + node.ID.Get()).c_str());
            ImGui::Checkbox("", &nodeData->onlyInt);
            ImGui::PopID();
        }
    } else if (node.type == NodeType::OneMinusX) {
        OneMinusXNode* nodeData = GetNodeData<OneMinusXNode>(node).value_or(nullptr);

        if (nodeData) {
            float oneMinusXValue = 0.0f;
            float xValue = 1.0f;

            if (IsPinLinked(node.Inputs.at(0).ID)) {
                if (node.Inputs.at(0).Value) {
                    xValue = *static_cast<float*>(node.Inputs.at(0).Value);
                    oneMinusXValue = 1.0f - xValue;
                    ImGui::Text(std::to_string(oneMinusXValue).c_str());
                }
            } else {
                ImGui::SetCursorPosX(cursorStartPos.x + padding);
                ImGui::SetNextItemWidth(370.0f);
                ImGui::SliderFloat("", &nodeData->x, 0, 1);
                xValue = nodeData->x;
                oneMinusXValue = 1.0f - xValue;
            }

            nodeData->x = xValue;

            node.Outputs.at(0).Value = new float(oneMinusXValue);
        }
    } else if (node.type == NodeType::Multiply) {
        MultiplyNode* nodeData = GetNodeData<MultiplyNode>(node).value_or(nullptr);

        if (nodeData) {
            float value, multiplier = 1.0f;

            if (IsPinLinked(node.Inputs.at(0).ID) && node.Inputs.at(0).Value)
                value = *static_cast<float*>(node.Inputs.at(0).Value);

            if (IsPinLinked(node.Inputs.at(1).ID) && node.Inputs.at(1).Value)
                multiplier = *static_cast<float*>(node.Inputs.at(1).Value);


            const float multipliedValue = value * multiplier;
            node.Outputs.at(0).Value = new float(multipliedValue);

            ImGui::SetCursorPosX(cursorStartPos.x + padding);
            ImGui::Text("Multiplied value: %f", multipliedValue);
        }
    }
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

void MaterialNodeSystem::DrawNode(Node& node) {
    ed::BeginNode(node.ID);
    DrawNodeTitle(node.Name, node.Size.x, node.Color, node.ID);

    constexpr float padding = 10.0f;

    ImGui::Dummy(ImVec2(0, padding));

    ImVec2 nodeStartPos = ImGui::GetCursorScreenPos() + ImVec2(0, 10);
    ImVec2 inputSectionWidth = ImVec2(node.InputSectionWidth, 0.0f);

    ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
    ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
    ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersBottom);

    for (size_t i = 0; i < node.Inputs.size(); ++i) {
        auto& inputPin = node.Inputs[i];
        ImGui::SetCursorScreenPos(nodeStartPos + ImVec2(padding, i * (m_PinIconSize + 5.0f)));

        ed::BeginPin(inputPin.ID, ed::PinKind::Input);
        ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
        ed::PinPivotSize(ImVec2(0, 0));

        DrawPinIcon(
            ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)),
            MaterialDrawing::IconType::Circle,
            false,
            ImColor(100, 100, 100, 255),
            ImColor(32, 32, 32, 100)
        );

        ed::EndPin();

        ImGui::SameLine(inputSectionWidth.x - ImGui::CalcTextSize(inputPin.Name.c_str()).x);
        ImGui::Text(inputPin.Name.c_str());
    }

    if (node.Inputs.empty())
        inputSectionWidth = ImVec2(0.0f, 0.0f);

    this->DrawNodeMiddleSection(node, nodeStartPos + inputSectionWidth);

    const ImVec2 outputStartPos    = nodeStartPos   + ImVec2(node.Size.x - 100.0f - padding, 0.0f);
    const float outputPinStartPosX = nodeStartPos.x + node.Size.x - m_PinIconSize - padding;

    for (size_t i = 0; i < node.Outputs.size(); ++i) {
        auto& outputPin = node.Outputs[i];

        ImGui::SetCursorScreenPos(outputStartPos + ImVec2(0.0f, i * (m_PinIconSize + 5.0f)));

        ImGui::Text(outputPin.Name.c_str());
        ImGui::SameLine();
        ImGui::SetCursorPosX(outputPinStartPosX);

        ed::BeginPin(outputPin.ID, ed::PinKind::Output);
        ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
        ed::PinPivotSize(ImVec2(0, 0));

        DrawPinIcon(
            ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)),
            MaterialDrawing::IconType::Circle,
            false,
            ImColor(100, 100, 100, 255),
            ImColor(32, 32, 32, 100)
        );

        ed::EndPin();

        if (IsPinLinked(outputPin.ID)) {
            std::vector<ed::PinId> inputPinsId = GetConnectedInputPins(outputPin.ID);
            for (ed::PinId pinID : inputPinsId) {
                Pin* inputPin = FindPin(pinID);
                inputPin->Value = outputPin.Value;
            }
        }
    }

    ImGui::SetCursorPosY(nodeStartPos.y + node.Size.y);
    ImGui::Dummy(ImVec2(0, padding));

    ed::PopStyleVar(3);
    ed::EndNode();
}


bool MaterialNodeSystem::ArePinsValid(Pin* startPin, Pin* endPin) {
    if (!startPin || !endPin) return false;
    if (startPin == endPin) return false;

    if (startPin->Type != endPin->Type) {
        DrawTextInNodeEditor("Pin Types Do Not Match.", true);
        return false;
    }

    if (startPin->Kind == endPin->Kind) {
        DrawTextInNodeEditor("Pin Kinds Are The Same.", true);
        return false;
    }

    return true;
}

void MaterialNodeSystem::HandleNewLink(ed::PinId& startPinId, ed::PinId& endPinId) {
    Pin* startPin = FindPin(startPinId);
    Pin* endPin = FindPin(endPinId);

    if (!ArePinsValid(startPin, endPin)) return;

    newLinkPin = startPin ? startPin : endPin;

    if (startPin->Kind == PinKind::Input) {
        std::swap(startPin, endPin);
        std::swap(startPinId, endPinId);
    }

    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
        m_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
    }
}

void MaterialNodeSystem::DrawMaterialNodeEditor(SurfaceMaterial& surfaceMaterial) {
    if (!m_Context)
        this->Init();

    if (!m_Context)
        return;

    auto& io = ImGui::GetIO();

    ed::SetCurrentEditor(m_Context);
    ed::Begin("Material Editor", ImVec2(0.0, 0.0f));

    ed::PushStyleColor(ed::StyleColor_NodeBg,        ImColor(128, 128, 128, 200));
    ed::PushStyleColor(ed::StyleColor_PinRect,       ImColor( 60, 180, 255, 150));
    ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImColor( 60, 180, 255, 150));

    ed::PushStyleVar(ed::StyleVar_NodePadding,     ImVec4(0, 0, 0, 0));
    ed::PushStyleVar(ed::StyleVar_NodeRounding,    5.0f);
    ed::PushStyleVar(ed::StyleVar_PinBorderWidth,  1.0f);
    ed::PushStyleVar(ed::StyleVar_PinRadius,       5.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
    ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f,  1.0f));
    ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);

    ImColor pinBackground = ImColor(128, 128, 128, 200);

    for (Node& node : m_Nodes) {
        this->DrawNode(node);
    }

    ed::PopStyleVar(8);
    ed::PopStyleColor(3);

    for (auto& link : m_Links) {
        ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);
    }

    if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
        ed::PinId startPinId = 0, endPinId = 0;

        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            HandleNewLink(startPinId, endPinId);
        } else {
            newLinkPin = nullptr;
        }
    }

    ed::EndCreate();

    this->ShowPopup();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void MaterialNodeSystem::ShowPopup() {
    ImVec2 openPopupPosition = ImGui::GetMousePos();


    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        showAddNode = true;
    }

    ed::Suspend();
    if (showAddNode) {
        ImGui::OpenPopup("Add Node");
    }

    if(ImGui::BeginPopup("Add Node")) {
        Node* node = nullptr;

        if (ImGui::BeginMenu("Input")) {
            if (ImGui::MenuItem("Texture")) {
                node = SpawnTextureNode();
            }

            if (ImGui::MenuItem("Color")) {
                node = SpawnColorNode();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Math")) {
            if (ImGui::MenuItem("Slider")) {
                node = SpawnSliderNode();
            }

            if (ImGui::MenuItem("Clamp"));// node = SpawnClampNode();

            if (ImGui::MenuItem("Multiply")) {
                node = SpawnMultiplyNode();
            }

            if (ImGui::MenuItem("1-x")) {
                node = SpawnOneMinusXNode();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Logic")) {

            ImGui::EndMenu();

        }

        if (ImGui::BeginMenu("Output")) {
            if (ImGui::MenuItem("Material")) {
                node = SpawnMaterialNode();
            }

            ImGui::EndMenu();
        }

        if (node) {
            showAddNode = false;

            ed::SetNodePosition(node->ID, openPopupPosition);
        }

        ImGui::EndPopup();
    }

    ed::Resume();
}

Node* MaterialNodeSystem::SpawnMaterialNode() {

}

Node* MaterialNodeSystem::SpawnColorNode() {
    ColorNode colorNode{ ImColor(255, 255, 255, 255) };
    m_Nodes.emplace_back(GetNextId(), "Color", colorNode, NodeType::Color, ImColor(255, 100, 100), ImVec2(450.0f, -1.0f));
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "Color", PinType::TextureOrColor, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}

Node* MaterialNodeSystem::SpawnTextureNode() {
    TextureNode textureNode;
    m_Nodes.emplace_back(GetNextId(), "Texture", textureNode, NodeType::Texture, ImColor(100, 255, 100), ImVec2(400.0f, -1.0f));
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "Texture", PinType::TextureOrColor, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}

Node* MaterialNodeSystem::SpawnSliderNode() {
    SliderNode sliderNode;
    m_Nodes.emplace_back(GetNextId(), "Slider", sliderNode, NodeType::Slider, ImColor(100, 100, 255), ImVec2(500.0f, -1.0f));
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "Number", PinType::Number, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}

Node* MaterialNodeSystem::SpawnOneMinusXNode() {
    OneMinusXNode oneMinusXNode;
    m_Nodes.emplace_back(GetNextId(), "1-x", oneMinusXNode, NodeType::OneMinusX, ImColor(100, 255, 255));
    m_Nodes.back().Inputs.emplace_back(GetNextId(), "X", PinType::Number, PinKind::Input);
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "1-x", PinType::Number, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}

Node* MaterialNodeSystem::SpawnMultiplyNode() {
    MultiplyNode multiplyNode;
    m_Nodes.emplace_back(GetNextId(), "Multiply", multiplyNode, NodeType::Multiply, ImColor(255, 255, 100), ImVec2(500, -1), 150.0f);
    m_Nodes.back().Inputs.emplace_back(GetNextId(), "Value", PinType::Number, PinKind::Input);
    m_Nodes.back().Inputs.emplace_back(GetNextId(), "Multiplier", PinType::Number, PinKind::Input);
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Number, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}