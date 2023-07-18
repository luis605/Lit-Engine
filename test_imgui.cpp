#include "include_all.h"


static ImTextureID icon_texture;






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