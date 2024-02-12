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
bool IsMouseHoveringModel(const Model& model, const Camera& camera, const Vector3& position, const Vector3& rotation, const Vector3& scale, const Entity* entity = nullptr, bool bypassOptimization = false);
float GetExtremeValue(const Vector3& a);

void openAboutPage();
void openManualPage();

struct LitCamera;


#endif // FUNCS_H