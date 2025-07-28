/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <Engine/Editor/MaterialNodeEditor/Drawing.hpp>
#include <Engine/Editor/MaterialNodeEditor/Helpers.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialGraph.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialShaderGenerator.hpp>
#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Core/UUID.hpp>
#include <algorithm>
#include <variant>
#include <imgui/misc/cpp/imgui_stdlib.h>

Texture2D noiseTexture;
bool showMaterialInNodesEditor;

namespace {

    void DrawColorNodeContent(ColorNode& data) {
        ImGui::SetNextItemWidth(200);
        ImGui::BeginDisabled();
        ImGui::ColorEdit4("##ColorValue", &data.color.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::EndDisabled();
    }

    void DrawSliderNodeContent(SliderNode& data) {
        ImGui::SetNextItemWidth(200);
        ImGui::BeginDisabled();

        ImGui::SliderFloat("Value", &data.value, -100.0f, 100.0f);
        ImGui::Checkbox("Integer Only", &data.onlyInt);
        ImGui::EndDisabled();
    }

    void DrawTextureNodeContent(TextureNode& data) {
        ImGui::BeginDisabled();
        ImGui::Button("Texture", ImVec2(200, 0));
        ImGui::EndDisabled();
    }

    void DrawVector2NodeContent(Vector2Node& data) {
        ImGui::BeginDisabled();
        ImGui::Button("Slider", ImVec2(200, 0));
        ImGui::EndDisabled();
    }

} // namespace

void MaterialNodeSystem::Initialize() {
    if (!m_context) {
        m_context = ed::CreateEditor();
    }
}

void MaterialNodeSystem::Shutdown() {
    if (m_context) {
        ed::DestroyEditor(m_context);
        m_context = nullptr;
    }
}

int MaterialNodeSystem::GetNextId() {
    return m_nextId++;
}

ed::LinkId MaterialNodeSystem::GetNextLinkId() {
    return ed::LinkId(GetNextId());
}

Node* MaterialNodeSystem::FindNode(ed::NodeId id) {
    auto it = m_nodes.find(id);
    return it != m_nodes.end() ? &it->second : nullptr;
}

Link* MaterialNodeSystem::FindLink(ed::LinkId id) {
    auto it = m_links.find(id);
    return it != m_links.end() ? &it->second : nullptr;
}

Pin* MaterialNodeSystem::FindPin(ed::PinId id) {
    auto it = m_pins.find(id);
    return it != m_pins.end() ? &it->second : nullptr;
}

bool MaterialNodeSystem::IsPinLinked(ed::PinId id) {
    if (!id) return false;
    for (const auto& [linkId, link] : m_links) {
        if (link.m_startPinId == id || link.m_endPinId == id) {
            return true;
        }
    }
    return false;
}

void MaterialNodeSystem::AddPin(Node& node, Pin&& pin) {
    if (pin.m_kind == PinKind::Input) {
        node.m_inputs.push_back(pin.m_id);
    } else {
        node.m_outputs.push_back(pin.m_id);
    }
    m_pins.emplace(pin.m_id, std::move(pin));
}

void DrawPinIcon(const ImVec2& size, MaterialIconType type, bool filled, const ImVec4& color, const ImVec4& innerColor) {
    if (ImGui::IsRectVisible(size)) {
        auto cursorPos = ImGui::GetCursorScreenPos();
        auto drawList = ImGui::GetWindowDrawList();
        DrawMaterialNodeIcon(drawList, cursorPos, cursorPos + size, type, filled, ImColor(color), ImColor(innerColor));
    }
    ImGui::Dummy(size);
}

void MaterialNodeSystem::DrawNode(Node& node) {
    ed::BeginNode(node.m_id);

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    constexpr float headerHeight = 30.0f;
    ImVec2 headerMin = cursorPos;
    ImVec2 headerMax = ImVec2(headerMin.x + node.m_size.x, headerMin.y + headerHeight);
    const float uvWidth = std::min(node.m_size.x / noiseTexture.width, 1.0f);
    const float uvHeight = std::min(headerHeight / noiseTexture.height, 1.0f);

    ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)&noiseTexture, headerMin, headerMax, ImVec2(0.0f, 0.0f), ImVec2(uvWidth, uvHeight), ImGui::ColorConvertFloat4ToU32(node.m_color), 5.0f, ImDrawFlags_RoundCornersTop);

    ImVec2 textSize = ImGui::CalcTextSize(node.m_name.c_str());
    ImGui::SetCursorScreenPos(headerMin + ImVec2((node.m_size.x - textSize.x) * 0.5f, (headerHeight - textSize.y) * 0.5f));
    ImGui::TextUnformatted(node.m_name.c_str());
    ImGui::SetCursorScreenPos(cursorPos);

    std::string invisibleButtonId = "##node_header_" + node.m_uuid;
    ImGui::InvisibleButton(invisibleButtonId.c_str(), ImVec2(node.m_size.x, headerHeight));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_contextNodeId = node.m_id;
        m_openContextMenu = true;
    }

    ImGui::GetWindowDrawList()->AddLine(ImVec2(headerMin.x, headerMax.y), headerMax, ImColor(100, 100, 100, 250), 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(headerMin.x, headerMax.y));

    constexpr float padding = 10.0f;
    ImGui::Dummy(ImVec2(0, padding));

    ImVec2 contentStartPos = ImGui::GetCursorScreenPos();
    float maxPinHeight = std::max(node.m_inputs.size(), node.m_outputs.size()) * (m_pinIconSize + 5.0f);

    ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
    ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
    ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersBottom);

    for (size_t i = 0; i < node.m_inputs.size(); ++i) {
        Pin* inputPin = FindPin(node.m_inputs[i]);
        if (!inputPin) continue;

        ImGui::SetCursorScreenPos(contentStartPos + ImVec2(padding, i * (m_pinIconSize + 5.0f)));
        ed::BeginPin(inputPin->m_id, ed::PinKind::Input);
        ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
        DrawPinIcon(ImVec2(m_pinIconSize, m_pinIconSize), MaterialIconType::Circle, IsPinLinked(inputPin->m_id), ImColor(100, 100, 100, 255), ImColor(32, 32, 32, 100));
        ed::EndPin();

        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0, (m_pinIconSize - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(inputPin->m_name.c_str());
    }

    for (size_t i = 0; i < node.m_outputs.size(); ++i) {
        Pin* outputPin = FindPin(node.m_outputs[i]);
        if (!outputPin) continue;

        ImGui::SetCursorScreenPos(contentStartPos + ImVec2(node.m_size.x - padding, i * (m_pinIconSize + 5.0f)));

        textSize = ImGui::CalcTextSize(outputPin->m_name.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - textSize.x - padding);
        ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + ImVec2(0, (m_pinIconSize - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(outputPin->m_name.c_str());

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padding - m_pinIconSize);

        ed::BeginPin(outputPin->m_id, ed::PinKind::Output);
        ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
        DrawPinIcon(ImVec2(m_pinIconSize, m_pinIconSize), MaterialIconType::Circle, IsPinLinked(outputPin->m_id), ImColor(100, 100, 100, 255), ImColor(32, 32, 32, 100));
        ed::EndPin();
    }

    ed::PopStyleVar(3);

    ImGui::SetCursorScreenPos(contentStartPos + ImVec2(node.m_inputSectionWidth, 0));
    ImGui::BeginGroup();
    std::visit([&](auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, ColorNode>) DrawColorNodeContent(data);
        else if constexpr (std::is_same_v<T, SliderNode>) DrawSliderNodeContent(data);
        else if constexpr (std::is_same_v<T, TextureNode>) DrawTextureNodeContent(data);
        else if constexpr (std::is_same_v<T, Vector2Node>) DrawVector2NodeContent(data);
    }, node.m_data);
    ImGui::EndGroup();

    if (node.m_size.y < 0) {
        ImGui::SetCursorScreenPos(contentStartPos);
        float contentHeight = ImGui::GetItemRectMax().y - contentStartPos.y;
        node.m_size.y = std::max(maxPinHeight, contentHeight) + padding;
        ed::SetNodePosition(node.m_id, ed::GetNodePosition(node.m_id));
    }

    ed::EndNode();
}

bool MaterialNodeSystem::ArePinsCompatible(Pin* startPin, Pin* endPin) {
    if (!startPin || !endPin || startPin == endPin || startPin->m_kind == endPin->m_kind || startPin->m_ownerId == endPin->m_ownerId) {
        return false;
    }

    for (const auto& startType : startPin->m_type) {
        for (const auto& endType : endPin->m_type) {
            if (startType == endType) return true;
        }
    }

    DrawTextInNodeEditor("Pin Types Do Not Match.", true);
    return false;
}

void MaterialNodeSystem::HandleConnection(ed::PinId startPinId, ed::PinId endPinId) {
    Pin* startPin = FindPin(startPinId);
    Pin* endPin = FindPin(endPinId);

    if (!ArePinsCompatible(startPin, endPin)) return;

    if (startPin->m_kind == PinKind::Input) {
        std::swap(startPin, endPin);
        std::swap(startPinId, endPinId);
    }

    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
        ed::LinkId newLinkId = GetNextLinkId();
        m_links.emplace(newLinkId, Link(newLinkId, startPinId, endPinId));
    }
}

void MaterialNodeSystem::DrawEditor() {
    if (!m_context) Initialize();
    if (!m_context) return;

    ed::SetCurrentEditor(m_context);
    ed::Begin("Material Editor", ImVec2(0.0, 0.0f));

    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(30, 30, 30, 220));
    ed::PushStyleColor(ed::StyleColor_PinRect, ImColor(60, 180, 255, 150));
    ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImColor(60, 180, 255, 150));

    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 5.0f);
    ed::PushStyleVar(ed::StyleVar_PinBorderWidth, 1.0f);
    ed::PushStyleVar(ed::StyleVar_PinRadius, 5.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
    ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(1.0f, 0.0f));
    ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(-1.0f, 0.0f));
    ed::PushStyleVar(ed::StyleVar_LinkStrength, 150.0f);

    for (auto& [id, node] : m_nodes) {
        DrawNode(node);
    }

    for (const auto& [id, link] : m_links) {
        ed::Link(link.m_id, link.m_startPinId, link.m_endPinId, link.m_color, 2.0f);
    }

    if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
        ed::PinId startPinId = 0, endPinId = 0;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            HandleConnection(startPinId, endPinId);
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::NodeId deletedNodeId;
        while (ed::QueryDeletedNode(&deletedNodeId)) {
            DeleteNode(deletedNodeId);
        }
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            DeleteLink(deletedLinkId);
        }
    }
    ed::EndDelete();

    ed::Suspend();
    ShowNodeContextMenu();
    ShowBackgroundContextMenu();
    ed::Resume();

    ed::PopStyleVar(8);
    ed::PopStyleColor(3);

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void MaterialNodeSystem::ShowNodeContextMenu() {
    static bool shouldSetPopupPos = false;
    static bool shouldRename = false;
    static ImVec2 popupPosition;

    if (m_openContextMenu) {
        popupPosition = ImGui::GetMousePos();
        shouldSetPopupPos = true;
        m_openContextMenu = false;
        ImGui::OpenPopup("NodeContextMenu");
    }

    if (ImGui::BeginPopup("NodeContextMenu")) {
        if (shouldSetPopupPos) {
            ImGui::SetWindowPos(popupPosition, ImGuiCond_Always);
            shouldSetPopupPos = false;
        }

        Node* node = FindNode(m_contextNodeId);
        if (node) {
            if (shouldRename) {
                if (ImGui::InputText("##Rename", &node->m_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    node->m_name = node->m_renameBuffer;
                    shouldRename = false;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                if (ImGui::MenuItem("Delete Node")) {
                    DeleteNode(node->m_id);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Rename Node")) {
                    shouldRename = true;
                    m_openContextMenu = true;
                }
            }
        }

        ImGui::EndPopup();
    }
}

void MaterialNodeSystem::ShowBackgroundContextMenu() {
    static bool shouldSetPopupPos = false;
    static ImVec2 popupPosition;

    if (ed::ShowBackgroundContextMenu() || (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_A))) {
        popupPosition = ImGui::GetMousePos();
        shouldSetPopupPos = true;
        ImGui::OpenPopup("Create New Node");
    }

    if (ImGui::BeginPopup("Create New Node")) {
        if (shouldSetPopupPos) {
            ImGui::SetWindowPos(popupPosition, ImGuiCond_Always);
            shouldSetPopupPos = false;
        }

        Node* newNode = nullptr;

        if (ImGui::BeginMenu("Input")) {
            if (ImGui::MenuItem("Texture")) newNode = SpawnTextureNode();
            if (ImGui::MenuItem("Color")) newNode = SpawnColorNode();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Math")) {
            if (ImGui::MenuItem("Slider")) newNode = SpawnSliderNode();
            if (ImGui::MenuItem("Multiply")) newNode = SpawnMultiplyNode();
            if (ImGui::MenuItem("1-x")) newNode = SpawnOneMinusXNode();
            if (ImGui::MenuItem("Vector 2")) newNode = SpawnVector2Node();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Output")) {
            if (ImGui::MenuItem("Material")) newNode = SpawnMaterialNode();
            ImGui::EndMenu();
        }

        if (newNode) {
            ed::SetNodePosition(newNode->m_id, ed::ScreenToCanvas(popupPosition));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MaterialNodeSystem::DeleteNode(ed::NodeId nodeId) {
    Node* node = FindNode(nodeId);
    if (!node) return;

    for (ed::PinId pinId : node->m_inputs) m_pins.erase(pinId);
    for (ed::PinId pinId : node->m_outputs) m_pins.erase(pinId);

    std::vector<ed::LinkId> linksToDelete;
    for(auto& [linkId, link] : m_links) {
        bool startPinInNode = std::find(node->m_outputs.begin(), node->m_outputs.end(), link.m_startPinId) != node->m_outputs.end();
        bool endPinInNode = std::find(node->m_inputs.begin(), node->m_inputs.end(), link.m_endPinId) != node->m_inputs.end();
        if(startPinInNode || endPinInNode){
            linksToDelete.push_back(linkId);
        }
    }
    for(ed::LinkId linkId : linksToDelete){
        DeleteLink(linkId);
    }

    m_nodes.erase(nodeId);
}

void MaterialNodeSystem::DeleteLink(ed::LinkId linkId) {
    m_links.erase(linkId);
}

void MaterialNodeSystem::AddLink(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId) {
     m_links.emplace(id, Link(id, startPinId, endPinId));
}

Node* MaterialNodeSystem::SpawnMaterialNode() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "Material", NodeType::Material, MaterialNode{}, ImColor(180, 60, 60), ImVec2(250.0f, 250.0f), 150.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "Diffuse", PinType::TextureOrColor, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "Normal", PinType::TextureOrColor, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "Roughness", PinType::Number, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "Metallic", PinType::Number, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "AO", PinType::Number, PinKind::Input));
    node.m_isRoot = true;
    return &node;
}

Node* MaterialNodeSystem::SpawnColorNode() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "Color", NodeType::Color, ColorNode{ImColor(1.0f, 1.0f, 1.0f, 1.0f)}, ImColor(200, 100, 100), ImVec2(400.0f, -1.0f), 100.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "Out", PinType::TextureOrColor, PinKind::Output));
    return &node;
}

Node* MaterialNodeSystem::SpawnTextureNode() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "Texture", NodeType::Texture, TextureNode{}, ImColor(100, 200, 100), ImVec2(400.0f, -1.0f), 100.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "Out", PinType::TextureOrColor, PinKind::Output));
    return &node;
}

Node* MaterialNodeSystem::SpawnSliderNode() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "Slider", NodeType::Slider, SliderNode{}, ImColor(100, 100, 200), ImVec2(400.0f, -1.0f), 100.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "Out", PinType::Number, PinKind::Output));
    return &node;
}

Node* MaterialNodeSystem::SpawnOneMinusXNode() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "1-x", NodeType::OneMinusX, OneMinusXNode{}, ImColor(100, 200, 200), ImVec2(250.0f, 80.0f), 100.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "In", PinType::Number, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "Out", PinType::Number, PinKind::Output));
    return &node;
}

Node* MaterialNodeSystem::SpawnMultiplyNode() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "Multiply", NodeType::Multiply, MultiplyNode{}, ImColor(200, 200, 100), ImVec2(300.0f, 100.0f), 150.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "A", {PinType::Number, PinType::TextureOrColor}, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "B", {PinType::Number, PinType::TextureOrColor}, PinKind::Input));
    AddPin(node, Pin(GetNextId(), nodeId, "Out", {PinType::Number, PinType::TextureOrColor}, PinKind::Output));
    return &node;
}

Node* MaterialNodeSystem::SpawnVector2Node() {
    int nodeId = GetNextId();
    auto& node = m_nodes.emplace(nodeId, Node(nodeId, "Vector 2", NodeType::Vector2, Vector2Node{}, ImColor(100, 200, 200), ImVec2(400.0f, -1.0f), 100.0f)).first->second;
    AddPin(node, Pin(GetNextId(), nodeId, "Out", PinType::Vector2, PinKind::Output));
    return &node;
}
