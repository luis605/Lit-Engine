#include <print>
import application;

int main() {
    Application app;
    while (app.isRunning()) {
        app.update();
    }
    return 0;
}
