import std;
import Editor.application;

int main() {
    Application app;
    while (app.isRunning()) {
        app.update();
    }
    return 0;
}
