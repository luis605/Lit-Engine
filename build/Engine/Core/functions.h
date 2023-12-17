#ifndef FUNCS_H
#define FUNCS_H

#ifndef GAME_SHIPPING
    #include "../include_all.h"
#endif


void InitGameCamera();
void RunGame();
void Inspector();
void CleanUp();
void AddLight();
void MenuBar();
void CleanScriptThreads(vector<std::thread>& script_threads);
int BuildProject();
bool IsMouseHoveringModel(Model model, Camera camera, Vector3 position, Vector3 rotation, Vector3 scale, Entity* entity = nullptr, bool bypass_optimization = false);
float GetExtremeValue(const Vector3& a);

struct LitCamera;


#endif // FUNCS_H