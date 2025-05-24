/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Tooltip.hpp"

void Tooltip::Draw() {
    DrawRectangleRounded({ position.x, position.y, size.x, size.y }, 0.5f, 10, backgroundColor);
    DrawText(text.c_str(), static_cast<int>(position.x) + 5, static_cast<int>(position.y) + 5, font.baseSize, textColor);
}
