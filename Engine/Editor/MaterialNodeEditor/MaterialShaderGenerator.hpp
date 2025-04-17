#ifndef MATERIAL_SHADER_GEN_H
#define MATERIAL_SHADER_GEN_H

#include <raylib.h>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>

std::string GenerateMaterialShader(ChildMaterial& material);

#endif // MATERIAL_SHADER_GEN_H