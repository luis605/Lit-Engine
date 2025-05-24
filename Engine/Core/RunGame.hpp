/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef RUN_GAME_H
#define RUN_GAME_H

#include <Engine/Core/Entity.hpp>
#include <Engine/Scripting/functions.hpp>

void InitGameCamera();
void RenderAndRunEntity(Entity& entity, LitCamera* rendering_camera = &camera);
void RunGame();

#endif // RUN_GAME_H