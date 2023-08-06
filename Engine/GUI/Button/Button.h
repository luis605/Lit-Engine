#include "../../../include_all.h"

class LitButton;
vector<LitButton> lit_buttons;
void DrawButtons();
LitButton &AddButton(const char* button_text, Vector3 position, Vector2 size, float text_size = 20);