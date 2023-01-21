
/* Button */

bool Button(int x, int y, int scale_x, int scale_y, char *text)
{
    return GuiButton((Rectangle){ x, y, scale_x, scale_y }, text);
    //return GuiButton((Rectangle){ 100, 100, 200, 50 }, "Hello world");
}
