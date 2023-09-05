#ifndef LITBUTTONS_H
#define LITBUTTONS_H

#include "../../../include_all.h"

class LitButton;
vector<LitButton> lit_buttons;
void DrawButtons();
LitButton &AddButton(const char* button_text, Vector3 position, Vector2 size, float text_size = 20);

#endif // LITBUTTONS_H