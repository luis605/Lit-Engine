#include "LivingMaterial.hpp"
#include "MaterialLayer.hpp"

LivingMaterial::LivingMaterial() {
    MaterialLayer substrate;
    substrate.m_Name = "Substrate";
    substrate.m_IsEnabled = true;
    this->m_Layers.push_back(substrate);
}