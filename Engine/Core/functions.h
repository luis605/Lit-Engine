#ifndef FUNCS_H
#define FUNCS_H

void InitGameCamera();
void RunGame();
void Inspector();
void CleanUp();
void AddLight();
void MenuBar();
void CleanScriptThreads(std::vector<std::thread>& script_threads);
void openAboutPage();
void openManualPage();
int BuildProject();
bool IsMouseHoveringModel(const Model& model, const Vector3& position, const Vector3& rotation, const Vector3& scale, const Entity* entity = nullptr, bool bypassOptimization = false);

#endif // FUNCS_H