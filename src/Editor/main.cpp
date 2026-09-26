import Editor.application;

int main() {
    Application app;
    while (app.isRunning()) {
        app.update();
    }
    return app.exitCode();
}
