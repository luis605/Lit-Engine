#include "raylib.h"

#define BSPLINE_LINE_DIVISIONS 100



struct DraggablePoint {
    Vector2 position;
    bool isDragging;
};



static void DrawLineBSpline(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color)
{
    float a[4] = { 0 };
    float b[4] = { 0 };

    a[0] = (-p1.x + 3*p2.x - 3*p3.x + p4.x)/6.0f;
    a[1] = (3*p1.x - 6*p2.x + 3*p3.x)/6.0f;
    a[2] = (-3*p1.x + 3*p3.x)/6.0f;
    a[3] = (p1.x + 4*p2.x + p3.x)/6.0f;
    b[0] = (-p1.y + 3*p2.y - 3*p3.y + p4.y)/6.0f;
    b[1] = (3*p1.y - 6*p2.y + 3*p3.y)/6.0f;
    b[2] = (-3*p1.y + 3*p3.y)/6.0f;
    b[3] = (p1.y + 4*p2.y + p3.y)/6.0f;

    Vector2 currentPoint = { 0 };
    Vector2 nextPoint = { 0 };
    currentPoint.x = a[3];
    currentPoint.y = b[3];

    float t = 0.0f;
    for (int i = 1; i < BSPLINE_LINE_DIVISIONS; i++)
    {
        t = ((float)i)/((float)BSPLINE_LINE_DIVISIONS);

        nextPoint.x = a[3] + t*(a[2] + t*(a[1] + t*a[0]));
        nextPoint.y = b[3] + t*(b[2] + t*(b[1] + t*b[0]));

        DrawLineEx(currentPoint, nextPoint, 10, color);

        currentPoint = nextPoint;
    }
}

int main()
{
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "B-Spline Example");
    
    DraggablePoint points[] = {
        { { 100, 300 }, false },
        { { 200, 100 }, false },
        { { 400, 500 }, false },
        { { 600, 300 }, false }
    };
    
    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        for (int i = 0; i < 4; i++)
        {
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointCircle(GetMousePosition(), points[i].position, 5))
            {
                points[i].isDragging = true;
            }
            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
            {
                points[i].isDragging = false;
            }
            if (points[i].isDragging)
            {
                points[i].position = GetMousePosition();
            }
        }

        BeginDrawing();

        ClearBackground(RAYWHITE);

        for (int i = 0; i < 3; i++)
        {
            DrawCircleV(points[i].position, 5, RED);
            DrawLineV(points[i].position, points[i + 1].position, DARKGRAY);
        }
        DrawCircleV(points[3].position, 5, RED);
        
        // Draw B-spline curve
        DrawLineBSpline(points[0].position, points[1].position, points[2].position, points[3].position, 2, BLUE);

        EndDrawing();
    }

    CloseWindow();

    return 0;
}