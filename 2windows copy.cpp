#include <unistd.h>
#include <raylib.h>

int main()
{
    int pid = fork();

    if (pid == -1)
    {
        // Error: Failed to create new process
        return 1;
    }
    else if (pid == 0)
    {
        // Child process

        // Initialize window in child process
        InitWindow(400, 300, "Window 1");

        while (!WindowShouldClose())
        {
            BeginDrawing();

            ClearBackground(RAYWHITE);
            DrawText("Window 1", 190, 140, 20, LIGHTGRAY);

            EndDrawing();
        }

        // Close window in child process
        CloseWindow();
    }
    else
    {
        // Parent process

        // Initialize window in parent process
        InitWindow(400, 300, "Window 2");

        while (!WindowShouldClose())
        {
            BeginDrawing();

            ClearBackground(RAYWHITE);
            DrawText("Window 2", 190, 140, 20, LIGHTGRAY);

            EndDrawing();
        }

        // Close window in parent process
        CloseWindow();
    }

    return 0;
}
