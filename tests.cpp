//Made by I, ARCHION

#include <raylib.h>
#include <string>
#include <cmath>
#include <vector>

using namespace std;

void QuestionStateBar(int length, int mult);

class Button
{
public:
    Rectangle rec;
    Color bg_color;
    string answer;
    float roundness = 4;
    bool Rounded;
    float fontSize = 30;

    Button() {
        bg_color = BLUE;
        Rounded = true;
    };

    void DrawButton()
    {
        if (!Rounded) {
            DrawRectangle(rec.x, rec.y, rec.width, rec.height, bg_color);
        }
        else {
            DrawRectangleRounded(rec, roundness, 70, bg_color);
        }

        // Center the text within the button
        float textX = rec.x + (rec.width - MeasureText(answer.c_str(), fontSize)) / 2;
        float textY = rec.y + (rec.height - fontSize) / 2;
        DrawText(answer.c_str(), textX, textY, fontSize, YELLOW);
    }

    int ButtonHandler(int whereami) {
        bg_color = BLUE;
        bool hovered = CheckCollisionPointRec(GetMousePosition(), Rectangle{ rec.x, rec.y, rec.width, rec.height });
        if (hovered) {
            bg_color = Color{ (unsigned char)(bg_color.r / 1.5),(unsigned char)(bg_color.g / 1.5),(unsigned char)(bg_color.b / 1.5), bg_color.a };
        }
        if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bg_color = Color{ (unsigned char)(bg_color.r / 1.5),(unsigned char)(bg_color.g / 1.5),(unsigned char)(bg_color.b / 1.5), bg_color.a };
            return whereami + 1;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hovered) {
            bg_color = Color{ (unsigned char)(bg_color.r / 1.5),(unsigned char)(bg_color.g / 1.5),(unsigned char)(bg_color.b / 1.5), bg_color.a };
        }
        return whereami;
    };
};

class Text
{
public:
    string txt;
    int z_index;
    Text() {};
private:
};

class Questions {
public:
    Text question;
    vector<Button> btn;

    Questions() {}

    void AddButton(string answer, Rectangle rec) {
        if (btn.size() < 4) {
            Button newButton;
            newButton.answer = answer;
            newButton.rec = rec;
            btn.push_back(newButton);
        }
    }
};

int main()
{
    Questions questions[3];
    questions[0].question.txt = "How are you?";
    questions[0].AddButton("Good", Rectangle{ 100, 100, 320, 60 });
    questions[0].AddButton("Bad", Rectangle{ 600, 100, 320, 60 });
    questions[0].AddButton("Kys", Rectangle{ 100, 300, 320, 60 });
    questions[0].AddButton("I won't answer", Rectangle{ 600, 300, 420, 60 });

    questions[1].question.txt = "Do you like this program";
    questions[1].AddButton("FIVE STAR", Rectangle{ 100, 100, 320, 60 });
    questions[1].AddButton("its good", Rectangle{ 600, 100, 320, 60 });
    questions[1].AddButton("bad", Rectangle{ 100, 300, 320, 60 });
    questions[1].AddButton("quit programming", Rectangle{ 600, 300, 320, 60 });

    questions[2].question.txt = "Thank you for participating";

    int whereami = 0;

    InitWindow(1200, 800, "STUPID SOUP");

    SetTargetFPS(60);

    while (!WindowShouldClose()) {

        BeginDrawing();
        ClearBackground(Color{ 194, 69, 45, 255 });
        QuestionStateBar(sizeof(questions) / sizeof(Questions), whereami);
        DrawText(questions[whereami].question.txt.c_str(), 20, 50, 30, Color{ 200, 193, 0, 255 });

        for (const auto& button : questions[whereami].btn) {
            whereami = button.ButtonHandler(whereami);
            button.DrawButton();
        }

        EndDrawing();
    }

    CloseWindow();
}

void QuestionStateBar(int length, int mult) {
    mult += 1;
    DrawRectangle(GetScreenWidth() / 2 - 50, 500, 100, 50, BROWN);
    DrawRectangle(GetScreenWidth() / 2 - 49, 502, (double)97 / length * mult + 1, 46, BLUE);
    string text = std::to_string(mult) + "/" + std::to_string(length);
    DrawText(text.c_str(), GetScreenWidth() / 2 - text.length() * 9, 470, 25, Color{ 255, 193, 0, 255 });
}
