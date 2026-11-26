#pragma once

#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineStateCache.h"

namespace litgpu {
inline Diligent::IPipelineStateCache* g_pipelineCache = nullptr;
inline Diligent::IPipelineStateCache* pipelineCache() { return g_pipelineCache; }
}
