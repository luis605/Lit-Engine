#ifndef LITBUTTONS_H
#define LITBUTTONS_H

class LitButton;
std::vector<LitButton> litButtons;
void DrawButtons();
LitButton &AddButton(const char* button_text, Vector3 position, Vector2 size, float text_size = 20);

#endif // LITBUTTONS_H