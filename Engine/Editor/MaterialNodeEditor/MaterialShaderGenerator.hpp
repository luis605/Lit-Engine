/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_SHADER_GEN_H
#define MATERIAL_SHADER_GEN_H

#include <raylib.h>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <Engine/Core/Entity.hpp>

std::string GenerateMaterialShader(Entity& entity, ChildMaterial& material);

#endif // MATERIAL_SHADER_GEN_H