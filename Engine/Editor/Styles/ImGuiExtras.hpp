namespace ImGui {
    void CenteredText(const char* label, const ImVec2& sizeArg) {
        ImGuiWindow* window = GetCurrentWindow();

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 labelSize = CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.0f, labelSize.y + style.FramePadding.y * 2.0f);

        const ImVec2 pos2 = ImVec2((pos.x + size.x), (pos.y + size.y));
        const ImRect bb(pos, pos2);

        ItemSize(size, style.FramePadding.y);

        const ImVec2 posMin = ImVec2((bb.Min.x + style.FramePadding.x), (bb.Min.y + style.FramePadding.y));
        const ImVec2 posMax = ImVec2((bb.Max.x - style.FramePadding.x), (bb.Max.y - style.FramePadding.y));

        RenderTextClipped(posMin, posMax, label, NULL, &labelSize, style.ButtonTextAlign, &bb);
    }
}