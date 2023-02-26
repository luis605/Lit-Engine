#include <raylib.h>
#include <GLFW/glfw3.h>
#include <iostream>

int main() {
    // Initialize the two GLFW windows
    glfwInit();
    GLFWwindow* window1 = glfwCreateWindow(800, 600, "Window 1", NULL, NULL);
    GLFWwindow* window2 = glfwCreateWindow(800, 600, "Window 2", NULL, NULL);

    // Set the context for the first window
    glfwMakeContextCurrent(window1);
    SetConfigFlags(FLAG_MSAA_4X_HINT);  // Enable multisampling for better quality

    // Set the context for the second window
    glfwMakeContextCurrent(window2);
    SetConfigFlags(FLAG_MSAA_4X_HINT);

    // Main loop
    while (!glfwWindowShouldClose(window1)) {
        std::cout << "HI" << std::endl;
        glfwMakeContextCurrent(window1);
        BeginDrawing();
        DrawRectangle(0,0,300, 300, RED);
        EndDrawing();
    }

    // Close the windows and terminate GLFW
    glfwDestroyWindow(window1);
    glfwDestroyWindow(window2);
    glfwTerminate();

    return 0;
}
