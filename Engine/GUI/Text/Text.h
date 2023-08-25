#ifndef TEXT_H
#define TEXT_H
#include "../../../include_all.h"


struct Text
{
    std::string text;
    std::string name = "Text";
    LitVector3 position;
    float fontSize;
    float spacing=4;
    Color color;
    Color backgroundColor = {0,0,0,0};
    float backgroundRoundiness = 0;
    float padding = 25;
};

std::vector<Text> textElements;

#endif // TEXT_H