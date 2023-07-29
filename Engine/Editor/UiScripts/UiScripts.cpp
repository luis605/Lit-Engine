#include "../../include_all.h"


ImVec4 rgbaToImguiColor(int r, int g, int b, float a) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}