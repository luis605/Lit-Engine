#ifndef RUN_GAME_H
#define RUN_GAME_H

void InitGameCamera();
void RenderAndRunEntity(Entity& entity, LitCamera* rendering_camera = &camera);
void RunGame();

#endif // RUN_GAME_H