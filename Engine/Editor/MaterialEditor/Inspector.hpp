/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_INSPECTOR_HPP
#define MATERIAL_INSPECTOR_HPP

#include "LivingMaterial.hpp"

class InspectorPanel {
public:
    void Render(LivingMaterial& material, int selectedLayer);
};

#endif // MATERIAL_INSPECTOR_HPP