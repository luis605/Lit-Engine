#ifndef FUNCS_H
#define FUNCS_H

#include "../include_all.h"



void InitGameCamera();
void RunGame();
void Inspector();
void CleanUp();
void AddLight();
void MenuBar();
void CleanScriptThreads(vector<std::thread>& script_threads);


#endif // FUNCS_H