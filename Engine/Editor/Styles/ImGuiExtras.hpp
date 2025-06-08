/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef IMGUI_EXTRAS_HPP
#define IMGUI_EXTRAS_HPP

#include <imgui.h>

enum class MessageBoxType {
    Info,
    Warning
};

namespace ImGui {

    void DrawMessageBox(const char* message, MessageBoxType type = MessageBoxType::Info);
    bool ToggleButton(const char* str_id, bool& v);
    void CenteredText(const char* label, const ImVec2& sizeArg);
}

#endif // IMGUI_EXTRAS_HPP