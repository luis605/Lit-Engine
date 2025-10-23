import application;
#include <print>

int main() {
    Application app;
    while (app.isRunning()) {
        app.update();
    }
    return 0;
}
