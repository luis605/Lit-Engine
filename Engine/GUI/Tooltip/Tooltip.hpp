/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef TOOLTIP_H
#define TOOLTIP_H

#include <string>
#include <raylib.h>

class Tooltip {
public:
    std::string text;
    Vector2 position;
    Vector2 size;
    Color backgroundColor = DARKGRAY;
    Color textColor = WHITE;
    Font font = GetFontDefault();

    void Draw();
};

#endif // TOOLTIP_H