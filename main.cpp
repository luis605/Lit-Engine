#include "include_all.h"

int LitEngine()
{
    Startup();
    EngineMainLoop();
    CleanUp();

    return 0;
}

int main()
{
    LitEngine();
    return 0;
}