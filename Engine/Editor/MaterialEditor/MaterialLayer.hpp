/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_LAYER_HPP
#define MATERIAL_LAYER_HPP

#include <string>
#include <vector>
#include <map>
#include <Engine/Core/Math.hpp>
#include <Engine/Core/Filesystem.hpp>
#include "Mask.hpp"
#include "DynamicRule.hpp"

class MaterialLayer {
public:
    std::string m_Name;
    bool m_IsEnabled;
    std::string m_BaseMaterialID;
    std::map<std::string, Vector4> m_ParameterOverrides;
    Mask m_Mask;
    std::vector<DynamicRule> m_Rules;
};

#endif // MATERIAL_LAYER_HPP