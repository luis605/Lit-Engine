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
void BuildProject();

#endif // FUNCS_H