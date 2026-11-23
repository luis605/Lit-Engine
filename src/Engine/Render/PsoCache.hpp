#pragma once

#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineStateCache.h"

// Process-wide pipeline state cache (Diligent IPipelineStateCache = VkPipelineCache on Vulkan). The Renderer creates it
// from <shader cache dir>/pipelines_vk.bin at start-up and writes it back at shutdown (RendererInit.cpp); every PSO
// creation passes it in PipelineStateCreateInfo::pPSOCache. Null (no cache) is valid everywhere.
namespace litgpu {
inline Diligent::IPipelineStateCache* g_pipelineCache = nullptr;
inline Diligent::IPipelineStateCache* pipelineCache() { return g_pipelineCache; }
}
