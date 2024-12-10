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
    for (auto& node : m_Nodes)
        if (node.ID == id)
            return &node;

    return nullptr;
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

void MaterialNodeSystem::DrawNodeMiddleSection(Node& node) {
    if (node.type == NodeType::Color) {
        ColorNode* nodeData = GetNodeData<ColorNode>(node).value_or(nullptr);

        if (nodeData) {
            ImGuiColorEditFlags colorEditFlags =
                ImGuiColorEditFlags_NoTooltip |
                ImGuiColorEditFlags_NoLabel |
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_PickerHueBar;

            ImGui::SetNextItemWidth(200.0f);
            ImGui::ColorPicker4("Color", (float*)&nodeData->color, colorEditFlags);
        }
    }
}

void MaterialNodeSystem::DrawNode(Node& node) {
        ed::BeginNode(node.ID);
        DrawNodeTitle(node.Name, node.Size.x, node.Color, node.ID);

        ImGui::Dummy(ImVec2(0, 10.0f));

        ImGui::Columns(3, nullptr, false);

        ImGui::SetColumnWidth(0, 100.0f);
        ImGui::SetColumnWidth(1, node.Size.x - 100.0f * 2.0f);
        ImGui::SetColumnWidth(2, 100.0f);

        ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
        ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
        ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersBottom);

        // Draw Input Pins
        for (size_t i = 0; i < node.Inputs.size(); ++i) {
            auto& inputPin = node.Inputs[i];
            ed::BeginPin(inputPin.ID, ed::PinKind::Input);

            DrawPinIcon(
                ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)),
                MaterialDrawing::IconType::Circle,
                false,
                ImColor(100, 100, 100, 255),
                ImColor(32, 32, 32, 100)
            );

            auto rect = ImGui_GetItemRect();
            ed::PinPivotRect(rect.Min, rect.Max);
            ed::PinRect(rect.Min, rect.Max);
            ed::EndPin();

            ImGui::SameLine(100.0f - ImGui::CalcTextSize(inputPin.Name.c_str()).x - 5.0f);
            ImGui::Text(inputPin.Name.c_str());
        }

        ImGui::NextColumn();
        this->DrawNodeMiddleSection(node);
        ImGui::NextColumn();

        for (size_t i = 0; i < node.Outputs.size(); ++i) {
            auto& outputPin = node.Outputs[i];

            ImGui::Text(outputPin.Name.c_str());
            ImGui::SameLine(100.0f - m_PinIconSize - 5.0f);

            ed::BeginPin(outputPin.ID, ed::PinKind::Output);

            DrawPinIcon(
                ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)),
                MaterialDrawing::IconType::Circle,
                false,
                ImColor(100, 100, 100, 255),
                ImColor(32, 32, 32, 100)
            );

            auto rect = ImGui_GetItemRect();
            ed::PinPivotRect(rect.Min, rect.Max);
            ed::PinRect(rect.Min, rect.Max);
            ed::EndPin();
        }

        ed::PopStyleVar(3);
        ImGui::NextColumn();
        ImGui::Columns(1);

        ImGui::Dummy(ImVec2(0, 10.0f));

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

    ImColor pinBackground = ImColor(128, 128, 128, 200);

    for (Node& node : m_Nodes) {
        this->DrawNode(node);
    }

    ed::PopStyleVar(5);
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
    m_Nodes.emplace_back(GetNextId(), "Color", colorNode, NodeType::Color, ImColor(255, 100, 100));
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::TextureOrColor, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}

Node* MaterialNodeSystem::SpawnTextureNode() {
    TextureNode textureNode;
    m_Nodes.emplace_back(GetNextId(), "Texture", textureNode, NodeType::Texture, ImColor(100, 255, 100));
    m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::TextureOrColor, PinKind::Output);

    BuildNode(&m_Nodes.back());

    return &m_Nodes.back();
}

Node* MaterialNodeSystem::SpawnSliderNode() {

}

Node* MaterialNodeSystem::SpawnOneMinusXNode() {

}

Node* MaterialNodeSystem::SpawnMultiplyNode() {

}