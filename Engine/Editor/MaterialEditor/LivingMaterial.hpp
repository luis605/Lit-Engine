/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef LIVING_MATERIAL_HPP
#define LIVING_MATERIAL_HPP

#include <string>
#include <vector>
#include "MaterialLayer.hpp"

class LivingMaterial {
public:
    std::string m_Name;
    std::vector<MaterialLayer> m_Layers;

    LivingMaterial();
};

#endif // LIVING_MATERIAL_HPP