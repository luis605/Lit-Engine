#include "include_all.h"

int LitEngine() {
    Startup();
    std::cout << "Lit Engine Started\n\n";
    EngineMainLoop();
    CleanUp();

    return 0;
}

int main() {
    LitEngine();

    return 0;
}