/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_LAYER_STACK_HPP
#define MATERIAL_LAYER_STACK_HPP

#include "LivingMaterial.hpp"

class LayerStackPanel {
public:
    void Render(LivingMaterial& material);
    int GetSelectedLayer() const;

private:
    int m_SelectedLayer = 0;
};

#endif // MATERIAL_LAYER_STACK_HPP