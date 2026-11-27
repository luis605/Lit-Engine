module;

#include "RendererPrelude.hpp"
#include "DiligentCore/Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp"

#include <functional>

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.governor;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;
import Engine.Render.graph;

#include "RendererInternal.hpp"
#include "RendererGraph.hpp"

namespace {

using rg::Usage;

Diligent::RESOURCE_STATE StateOf(Usage usage) {
    switch (usage) {
        case Usage::ShaderResource: return Diligent::RESOURCE_STATE_SHADER_RESOURCE;
        case Usage::UnorderedAccess: return Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
        case Usage::RenderTarget: return Diligent::RESOURCE_STATE_RENDER_TARGET;
        case Usage::DepthWrite: return Diligent::RESOURCE_STATE_DEPTH_WRITE;
        case Usage::DepthRead: return Diligent::RESOURCE_STATE_DEPTH_READ;
        case Usage::CopySource: return Diligent::RESOURCE_STATE_COPY_SOURCE;
        case Usage::CopyDest: return Diligent::RESOURCE_STATE_COPY_DEST;
        case Usage::IndirectArgument: return Diligent::RESOURCE_STATE_INDIRECT_ARGUMENT;
        case Usage::VertexBuffer: return Diligent::RESOURCE_STATE_VERTEX_BUFFER;
        case Usage::IndexBuffer: return Diligent::RESOURCE_STATE_INDEX_BUFFER;
        case Usage::ConstantBuffer: return Diligent::RESOURCE_STATE_CONSTANT_BUFFER;
        case Usage::None: break;
    }
    return Diligent::RESOURCE_STATE_UNKNOWN;
}

Diligent::BIND_FLAGS RequiredTextureBind(Usage usage) {
    switch (usage) {
        case Usage::ShaderResource: return Diligent::BIND_SHADER_RESOURCE;
        case Usage::UnorderedAccess: return Diligent::BIND_UNORDERED_ACCESS;
        case Usage::RenderTarget: return Diligent::BIND_RENDER_TARGET;
        case Usage::DepthWrite:
        case Usage::DepthRead: return Diligent::BIND_DEPTH_STENCIL;
        default: return Diligent::BIND_NONE;
    }
}

Diligent::BIND_FLAGS RequiredBufferBind(Usage usage) {
    switch (usage) {
        case Usage::ShaderResource: return Diligent::BIND_SHADER_RESOURCE;
        case Usage::UnorderedAccess: return Diligent::BIND_UNORDERED_ACCESS;
        case Usage::IndirectArgument: return Diligent::BIND_INDIRECT_DRAW_ARGS;
        case Usage::VertexBuffer: return Diligent::BIND_VERTEX_BUFFER;
        case Usage::IndexBuffer: return Diligent::BIND_INDEX_BUFFER;
        case Usage::ConstantBuffer: return Diligent::BIND_UNIFORM_BUFFER;
        default: return Diligent::BIND_NONE;
    }
}

bool IsAttachmentState(Diligent::RESOURCE_STATE state) { return (state & (Diligent::RESOURCE_STATE_RENDER_TARGET | Diligent::RESOURCE_STATE_DEPTH_WRITE | Diligent::RESOURCE_STATE_DEPTH_READ)) != 0; }

void* Native(Diligent::ITexture* texture) { return static_cast<void*>(texture); }
void* Native(Diligent::IBuffer* buffer) { return static_cast<void*>(buffer); }

bool EnvFlag(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (!value || !value[0]) return fallback;
    return value[0] == '1' || value[0] == 'y' || value[0] == 'Y' || value[0] == 't' || value[0] == 'T';
}

void WriteText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

}

rg::TextureDesc GraphTextureDesc(const Diligent::TextureDesc& desc) {
    rg::TextureDesc out;
    out.width = desc.Width;
    out.height = desc.Height;
    out.arraySize = desc.ArraySize;
    out.mipLevels = desc.MipLevels;
    out.sampleCount = desc.SampleCount;
    out.format = static_cast<uint32_t>(desc.Format);
    out.bindFlags = static_cast<uint32_t>(desc.BindFlags);
    out.dimension = static_cast<uint32_t>(desc.Type);
    return out;
}

DiligentGraphBackend::DiligentGraphBackend(DiligentData& gpu, std::function<void(int)> beginTiming, std::function<void(int)> endTiming)
    : m_gpu(gpu), m_beginTiming(std::move(beginTiming)), m_endTiming(std::move(endTiming)), m_labels(EnvFlag("LIT_GPU_LABELS", false)) {}

void* DiligentGraphBackend::createTexture(const rg::TextureDesc& desc, std::string_view name) {
    const std::string label(name);
    Diligent::TextureDesc description;
    description.Name = label.c_str();
    description.Type = static_cast<Diligent::RESOURCE_DIMENSION>(desc.dimension);
    description.Width = desc.width;
    description.Height = desc.height;
    description.ArraySize = desc.arraySize;
    description.MipLevels = desc.mipLevels;
    description.SampleCount = desc.sampleCount;
    description.Format = static_cast<Diligent::TEXTURE_FORMAT>(desc.format);
    description.BindFlags = static_cast<Diligent::BIND_FLAGS>(desc.bindFlags);
    description.Usage = Diligent::USAGE_DEFAULT;
    Diligent::ITexture* texture = nullptr;
    m_gpu.pDevice->CreateTexture(description, nullptr, &texture);
    if (!texture) Lit::Log::Error("Render graph: failed to create transient texture '{}' ({}x{})", label, desc.width, desc.height);
    return Native(texture);
}

void* DiligentGraphBackend::createBuffer(const rg::BufferDesc& desc, std::string_view name) {
    const std::string label(name);
    Diligent::BufferDesc description;
    description.Name = label.c_str();
    description.Size = desc.size;
    description.BindFlags = static_cast<Diligent::BIND_FLAGS>(desc.bindFlags);
    description.Mode = static_cast<Diligent::BUFFER_MODE>(desc.mode);
    description.ElementByteStride = desc.stride;
    description.Usage = Diligent::USAGE_DEFAULT;
    Diligent::IBuffer* buffer = nullptr;
    m_gpu.pDevice->CreateBuffer(description, nullptr, &buffer);
    if (!buffer) Lit::Log::Error("Render graph: failed to create transient buffer '{}' ({} bytes)", label, desc.size);
    return Native(buffer);
}

void DiligentGraphBackend::destroy(void* native, rg::ResourceType type) {
    if (!native) return;
    if (type == rg::ResourceType::Texture) static_cast<Diligent::ITexture*>(native)->Release();
    else static_cast<Diligent::IBuffer*>(native)->Release();
}

void DiligentGraphBackend::transition(std::span<const rg::Barrier> barriers) {
    m_descs.clear();
    bool unbind = false;
    auto warnOnce = [&](std::string_view name, Usage usage) {
        ++m_skipped;
        const std::string key = std::format("{}:{}", name, rg::usageName(usage));
        if (std::ranges::find(m_warned, key) != m_warned.end()) return;
        m_warned.push_back(key);
        Lit::Log::Warn("Render graph: '{}' declared as {} but lacks the bind flag; transition skipped", name, rg::usageName(usage));
    };
    for (const rg::Barrier& barrier : barriers) {
        const Diligent::RESOURCE_STATE state = StateOf(barrier.usage);
        if (state == Diligent::RESOURCE_STATE_UNKNOWN || !barrier.native) continue;
        if (barrier.type == rg::ResourceType::Texture) {
            auto* texture = static_cast<Diligent::ITexture*>(barrier.native);
            const Diligent::TextureDesc& desc = texture->GetDesc();
            const Diligent::BIND_FLAGS required = RequiredTextureBind(barrier.usage);
            if (required != Diligent::BIND_NONE && (desc.BindFlags & required) == 0) {
                warnOnce(barrier.name, barrier.usage);
                continue;
            }
            const Diligent::RESOURCE_STATE current = texture->GetState();
            if (current == Diligent::RESOURCE_STATE_UNKNOWN) continue;
            if (current == state && state != Diligent::RESOURCE_STATE_UNORDERED_ACCESS) continue;
            if (IsAttachmentState(current) && !IsAttachmentState(state)) unbind = true;
            m_descs.push_back(Diligent::StateTransitionDesc{texture, Diligent::RESOURCE_STATE_UNKNOWN, state, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE});
        } else {
            auto* buffer = static_cast<Diligent::IBuffer*>(barrier.native);
            const Diligent::BIND_FLAGS required = RequiredBufferBind(barrier.usage);
            if (required != Diligent::BIND_NONE && (buffer->GetDesc().BindFlags & required) == 0) {
                warnOnce(barrier.name, barrier.usage);
                continue;
            }
            const Diligent::RESOURCE_STATE current = buffer->GetState();
            if (current == Diligent::RESOURCE_STATE_UNKNOWN) continue;
            if (current == state && state != Diligent::RESOURCE_STATE_UNORDERED_ACCESS) continue;
            m_descs.push_back(Diligent::StateTransitionDesc{buffer, Diligent::RESOURCE_STATE_UNKNOWN, state, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE});
        }
    }
    if (m_descs.empty()) return;
    if (unbind) UnbindRenderTargets(m_gpu.pImmediateContext);
    m_gpu.pImmediateContext->TransitionResourceStates(static_cast<Diligent::Uint32>(m_descs.size()), m_descs.data());
}

void DiligentGraphBackend::beginPass(std::string_view name) {
    if (!m_labels) return;
    m_label.assign(name);
    m_gpu.pImmediateContext->BeginDebugGroup(m_label.c_str());
}

void DiligentGraphBackend::endPass() {
    if (m_labels) m_gpu.pImmediateContext->EndDebugGroup();
}

uint64_t DiligentGraphBackend::bytes(const rg::TextureDesc& desc) const {
    const Diligent::TextureFormatAttribs& attribs = Diligent::GetTextureFormatAttribs(static_cast<Diligent::TEXTURE_FORMAT>(desc.format));
    const uint64_t texel = std::max<uint64_t>(static_cast<uint64_t>(attribs.ComponentSize) * attribs.NumComponents, 1u);
    return static_cast<uint64_t>(desc.width) * desc.height * desc.arraySize * texel;
}

RendererGraphState::~RendererGraphState() {
    if (backend) pool.clear(*backend);
}

RenderGraphInfo Renderer::renderGraphInfo() const {
    RenderGraphInfo info;
    info.active = renderGraphEnabled();
    if (m_diligent && m_diligent->graph) {
        info.stats = m_diligent->graph->stats;
        info.poolResources = m_diligent->graph->pool.size();
    }
    return info;
}

void Renderer::requestGraphDump(std::string path) {
    if (!m_diligent) return;
    if (!m_diligent->graph) m_diligent->graph = std::make_shared<RendererGraphState>();
    m_diligent->graph->dumpPath = path.empty() ? std::string("render_graph") : std::move(path);
}

std::string Renderer::lastGraphDump() const { return m_diligent && m_diligent->graph ? m_diligent->graph->lastDump : std::string(); }

void Renderer::addShadowPasses(rg::RenderGraph& graph, FrameGraphData& frame, uint32_t objectCount, bool timed) {
    const int scope = timed ? static_cast<int>(GpuPass::Shadows) : -1;
    const bool portalView = !timed;
    graph.addPass("Virtual Shadows", [&](rg::PassBuilder& pass) {
        pass.timing(scope);
        pass.read(frame.depth, Usage::ShaderResource);
        pass.read(frame.sceneUbo, Usage::None);
        pass.read(frame.objects, Usage::None);
        pass.read(frame.vertices, Usage::None);
        pass.write(frame.vsmAtlas, Usage::None);
    }, [this, objectCount, portalView](rg::PassContext&) {
        if (portalView) {
            for (bool& valid : m_diligent->shadowHiZValid) valid = false;
        }
        drawVirtualShadows(objectCount);
    });
    graph.addPass("Local Shadows", [&](rg::PassBuilder& pass) {
        pass.timing(scope);
        pass.read(frame.objects, Usage::None);
        pass.write(frame.phases[kPhaseShadow], Usage::None);
        pass.write(frame.localShadowMap, Usage::DepthWrite);
    }, [this, objectCount](rg::PassContext&) { drawLocalShadows(objectCount); });
    graph.addPass("Sun Cascades", [&](rg::PassBuilder& pass) {
        pass.timing(scope);
        pass.read(frame.objects, Usage::None);
        pass.write(frame.phases[kPhaseShadow], Usage::None);
        pass.write(frame.shadowMap, Usage::DepthWrite);
    }, [this, objectCount](rg::PassContext&) { drawCascadeShadows(objectCount); });
}

void Renderer::addGeometryPasses(rg::RenderGraph& graph, FrameGraphData& frame, int viewIndex, uint32_t objectCount, PortalHistory* history) {
    auto& gpu = *m_diligent;
    const bool main = viewIndex == 0;
    auto timed = [main](GpuPass pass) { return main ? static_cast<int>(pass) : -1; };
    const int previousFrame = static_cast<int>((m_currentFrame + NUM_FRAMES_IN_FLIGHT - 1) % NUM_FRAMES_IN_FLIGHT);
    Diligent::ITexture* previousHiZ = history ? history->hiz[history->previous].texture.RawPtr() : gpu.pHiZTextures[previousFrame].RawPtr();
    Diligent::ITexture* currentHiZ = history ? history->hiz[1 - history->previous].texture.RawPtr() : gpu.pHiZTextures[m_currentFrame].RawPtr();
    const uint64_t key = history ? history->key : 0;
    const rg::Handle hizPrevious = graph.importTexture(history ? rg::viewResourceName(key, std::format("hiz{}", history->previous)) : std::format("hiz{}", previousFrame), Native(previousHiZ));
    const rg::Handle hizCurrent = graph.importTexture(history ? rg::viewResourceName(key, std::format("hiz{}", 1 - history->previous)) : std::format("hiz{}", m_currentFrame), Native(currentHiZ));

    auto cull = [&](const char* name, GpuPass timing, int phase, rg::Handle hiz, Diligent::ITexture* texture) {
        graph.addPass(name, [&](rg::PassBuilder& pass) {
            pass.timing(timed(timing));
            pass.read(hiz, Usage::ShaderResource);
            pass.read(frame.sceneUbo, Usage::None);
            pass.read(frame.objects, Usage::None);
            pass.write(frame.visibilityBits, Usage::None);
            pass.write(frame.phases[phase], Usage::None);
        }, [this, phase, objectCount, texture](rg::PassContext&) { cullVisibilityPhase(phase, objectCount, texture); });
    };
    auto instanceCull = [&](const char* name, GpuPass timing, int phase, rg::Handle hiz, Diligent::ITexture* texture) {
        if (!instancingRequested()) return;
        graph.addPass(name, [&](rg::PassBuilder& pass) {
            pass.timing(timed(timing));
            pass.read(hiz, Usage::ShaderResource);
            pass.read(frame.sceneUbo, Usage::None);
            pass.read(frame.objects, Usage::None);
            pass.read(frame.instances, Usage::None);
            pass.write(frame.visibilityBits, Usage::None);
            pass.write(frame.phases[phase], Usage::None);
        }, [this, phase, texture, main](rg::PassContext&) { cullInstances(phase, texture, main ? (phase == kPhaseOpaque ? 0u : 1u) : 3u); });
    };
    auto commands = [&](const char* name, const char* meshletName, GpuPass timing, int phase, rg::Handle hiz) {
        graph.addPass(name, [&](rg::PassBuilder& pass) {
            pass.timing(timed(timing));
            pass.write(frame.phases[phase], Usage::None);
        }, [this, phase](rg::PassContext&) { buildVisibilityDraws(phase, 1, false); });
        graph.addPass(meshletName, [&](rg::PassBuilder& pass) {
            pass.timing(timed(timing));
            pass.read(frame.phases[phase], Usage::None);
            pass.read(hiz, Usage::ShaderResource);
            pass.write(frame.meshlets[phase], Usage::None);
        }, [this, phase](rg::PassContext&) { buildMeshletDraws(phase, 1); });
    };
    auto targets = [&frame](rg::PassBuilder& pass) {
        for (const rg::Handle target : frame.gbuffer) pass.write(target, Usage::RenderTarget);
        pass.write(frame.hdr, Usage::RenderTarget);
        pass.write(frame.depth, Usage::DepthWrite);
        pass.read(frame.vertices, Usage::None);
        pass.read(frame.indices, Usage::None);
    };
    auto draw = [&](const char* name, const char* meshletName, GpuPass timing, int phase, bool clear) {
        graph.addPass(name, [&](rg::PassBuilder& pass) {
            pass.timing(timed(timing));
            targets(pass);
            pass.read(frame.phases[phase], Usage::None);
        }, [this, phase, clear](rg::PassContext&) {
            bindSceneTargets(clear);
            drawVisibilityPhase(phase, false);
        });
        graph.addPass(meshletName, [&](rg::PassBuilder& pass) {
            pass.timing(timed(timing));
            targets(pass);
            pass.read(frame.meshlets[phase], Usage::None);
        }, [this, phase](rg::PassContext&) { drawMeshletSurfaces(phase, -1, 0); });
    };

    cull("Cull", GpuPass::Cull, kPhaseOpaque, hizPrevious, previousHiZ);
    instanceCull("Instance Cull", GpuPass::InstanceCull, kPhaseOpaque, hizPrevious, previousHiZ);
    commands("CmdGen", "Meshlet Cull", GpuPass::CommandGen, kPhaseOpaque, hizPrevious);
    draw("GBuffer", "Meshlet GBuffer", GpuPass::OpaqueDraw, kPhaseOpaque, true);
    graph.addPass("Hi-Z", [&](rg::PassBuilder& pass) {
        pass.timing(timed(GpuPass::HiZ));
        pass.read(frame.depth, Usage::ShaderResource);
        pass.write(hizCurrent, Usage::None);
    }, [this, history, viewIndex](rg::PassContext&) {
        auto& state = *m_diligent;
        UnbindRenderTargets(state.pImmediateContext);
        if (!history) {
            buildHiZ();
            return;
        }
        HiZSet& set = history->hiz[1 - history->previous];
        BuildHiZ(state, set.texture.RawPtr(), set.source, set.dest, m_renderWidth, m_renderHeight, m_maxMipLevel, portalDepthRegion(viewIndex));
    });
    cull("Retest Cull", GpuPass::RetestCull, kPhaseRetest, hizCurrent, currentHiZ);
    instanceCull("Instance Retest Cull", GpuPass::InstanceRetestCull, kPhaseRetest, hizCurrent, currentHiZ);
    commands("Retest CmdGen", "Meshlet Retest Cull", GpuPass::RetestCommandGen, kPhaseRetest, hizCurrent);
    draw("Retest GBuffer", "Meshlet Retest GBuffer", GpuPass::RetestDraw, kPhaseRetest, false);
    if (!main || frame.portalsActive) {
        graph.addPass("Portal Stats", [&](rg::PassBuilder& pass) {
            pass.read(frame.phases[kPhaseOpaque], Usage::None);
            pass.read(frame.phases[kPhaseRetest], Usage::None);
            pass.sideEffect();
        }, [this, viewIndex](rg::PassContext&) { recordPortalStats(viewIndex); });
    }
    if (main && m_obsActive) {
        graph.addPass("Observation", [&](rg::PassBuilder& pass) {
            pass.read(frame.depth, Usage::None);
            pass.sideEffect();
        }, [this](rg::PassContext&) {
            auto& state = *m_diligent;
            if (!state.observation.ready()) return;
            UnbindRenderTargets(state.pImmediateContext);
            state.observation.record(state.pImmediateContext, state.pSceneDepth.RawPtr(), m_currentFrame, m_obsQuery);
        });
    }
    addShadowPasses(graph, frame, objectCount, main);
    cull("Trans Cull", GpuPass::TransparentCull, kPhaseTransparent, hizCurrent, currentHiZ);
    commands("Trans CmdGen", "Meshlet Trans Cull", GpuPass::TransparentCommandGen, kPhaseTransparent, hizCurrent);
}

void Renderer::addResolvePasses(rg::RenderGraph& graph, FrameGraphData& frame, int viewIndex) {
    auto& gpu = *m_diligent;
    const PortalViewNode& node = m_portalFrame.nodes[static_cast<size_t>(viewIndex)];
    const rg::TextureDesc depthDesc = GraphTextureDesc(gpu.pSceneDepth->GetDesc());
    if (viewIndex > 0 && node.masked) {
        const rg::Handle scratch = graph.createTexture(rg::viewResourceName(node.key, "maskScratch"), depthDesc);
        graph.addPass("Portal Mask", [&](rg::PassBuilder& pass) {
            pass.write(scratch, Usage::None);
            pass.write(frame.depth, Usage::DepthWrite);
        }, [this, viewIndex, scratch](rg::PassContext& context) {
            auto& state = *m_diligent;
            if (auto* texture = context.as<Diligent::ITexture>(scratch)) {
                state.portalDepthScratch = texture;
                texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(state.pHiZSampler);
            }
            maskPortalView(viewIndex);
        });
    }
    const rg::Handle scratch = graph.createTexture(rg::viewResourceName(node.key, "resolveScratch"), depthDesc);
    graph.addPass("Portal Resolve", [&](rg::PassBuilder& pass) {
        for (const PortalSurface& surface : node.surfaces) {
            if (surface.child <= 0 || surface.child >= kMaxPortalViews || !frame.views[surface.child].rendered) continue;
            const FrameGraphData::ViewOutputs& child = frame.views[surface.child];
            pass.read(child.depth, Usage::ShaderResource);
            for (const rg::Handle target : child.gbuffer) pass.read(target, Usage::ShaderResource);
        }
        pass.write(scratch, Usage::CopyDest);
        for (const rg::Handle target : frame.gbuffer) pass.write(target, Usage::RenderTarget);
        pass.write(frame.hdr, Usage::RenderTarget);
        pass.write(frame.depth, Usage::None);
    }, [this, viewIndex, scratch](rg::PassContext& context) {
        auto& state = *m_diligent;
        if (auto* texture = context.as<Diligent::ITexture>(scratch)) {
            state.portalDepthScratch = texture;
            texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(state.pHiZSampler);
        }
        UnbindRenderTargets(state.pImmediateContext);
        resolvePortalDepth(viewIndex);
    });
}

void Renderer::addShadePasses(rg::RenderGraph& graph, FrameGraphData& frame, int viewIndex) {
    auto& gpu = *m_diligent;
    const bool main = viewIndex == 0;
    auto timed = [main](GpuPass pass) { return main ? static_cast<int>(pass) : -1; };
    auto lighting = [this](LightingStage stage) {
        std::vector<rg::ExternalAccess> externals;
        m_lighting.graphResources(stage, externals);
        return externals;
    };
    const PortalViewNode* node = frame.portalsActive ? &m_portalFrame.nodes[static_cast<size_t>(viewIndex)] : nullptr;
    auto readChildColors = [&frame, node](rg::PassBuilder& pass) {
        if (!node) return;
        for (const PortalSurface& surface : node->surfaces) {
            if (surface.child >= 0 && surface.child < kMaxPortalViews && frame.views[surface.child].rendered) pass.read(frame.views[surface.child].color, Usage::ShaderResource);
        }
    };
    auto shade = [this, viewIndex](int stage) {
        auto& state = *m_diligent;
        m_lighting.setViewRect(portalViewRect(viewIndex), portalDepthRegion(viewIndex));
        m_lighting.shade(state.pSceneDepth, state.pSceneUBO[m_currentFrame], stage);
    };

    const std::vector<rg::ExternalAccess> ao = lighting(LightingStage::AmbientOcclusion);
    graph.addPass("GTAO", [&](rg::PassBuilder& pass) {
        pass.timing(timed(GpuPass::AmbientOcclusion));
        pass.read(frame.depth, Usage::ShaderResource);
        pass.read(frame.gbuffer[1], Usage::ShaderResource);
        pass.read(frame.sceneUbo, Usage::None);
        pass.access(ao);
    }, [this, shade, main, portals = frame.portalsActive](rg::PassContext&) {
        if (main && !portals) UnbindRenderTargets(m_diligent->pImmediateContext);
        shade(0);
    });
    const std::vector<rg::ExternalAccess> lit = lighting(LightingStage::Lighting);
    graph.addPass("Lighting", [&](rg::PassBuilder& pass) {
        pass.timing(timed(GpuPass::Lighting));
        pass.read(frame.depth, Usage::ShaderResource);
        for (const rg::Handle target : frame.gbuffer) pass.read(target, Usage::ShaderResource);
        pass.read(frame.sceneUbo, Usage::None);
        pass.access(lit);
        pass.write(frame.hdr, Usage::UnorderedAccess);
    }, [shade](rg::PassContext&) { shade(1); });
    if (frame.portalsActive) {
        graph.addPass("Portal Composite Color", [&](rg::PassBuilder& pass) {
            readChildColors(pass);
            pass.write(frame.hdr, Usage::RenderTarget);
            pass.read(frame.depth, Usage::None);
        }, [this, viewIndex](rg::PassContext&) {
            auto& state = *m_diligent;
            state.portalCompositeWritesDepth = false;
            compositePortals(viewIndex);
            state.portalCompositeWritesDepth = true;
            UnbindRenderTargets(state.pImmediateContext);
        });
    }
    const std::vector<rg::ExternalAccess> reflections = lighting(LightingStage::Reflections);
    graph.addPass("Reflections", [&](rg::PassBuilder& pass) {
        pass.timing(timed(GpuPass::Reflections));
        pass.read(frame.depth, Usage::ShaderResource);
        for (const rg::Handle target : frame.gbuffer) pass.read(target, Usage::ShaderResource);
        pass.read(frame.sceneUbo, Usage::None);
        pass.access(reflections);
        pass.write(frame.hdr, Usage::None);
    }, [shade](rg::PassContext&) { shade(2); });
    if (frame.portalsActive) {
        if (main) {
            frame.effectsDepth = graph.createTexture("portalEffectsDepth", GraphTextureDesc(gpu.pSceneDepth->GetDesc()));
            graph.addPass("Portal Save Effects Depth", [&](rg::PassBuilder& pass) {
                pass.read(frame.depth, Usage::CopySource);
                pass.write(frame.effectsDepth, Usage::CopyDest);
            }, [this, handle = frame.effectsDepth](rg::PassContext& context) {
                auto& state = *m_diligent;
                if (auto* texture = context.as<Diligent::ITexture>(handle)) state.portalEffectsDepth = texture;
                SavePortalEffectsDepth(state);
            });
        }
        graph.addPass("Portal Composite", [&](rg::PassBuilder& pass) {
            readChildColors(pass);
            pass.write(frame.hdr, Usage::RenderTarget);
            pass.write(frame.depth, Usage::DepthWrite);
        }, [this, viewIndex](rg::PassContext&) { compositePortals(viewIndex); });
    }
}

void Renderer::renderFrameGraph(SceneDatabase& sceneDatabase, const Camera& camera, float currentFrameTime, float deltaTime, uint32_t objectCount) {
    auto& gpu = *m_diligent;
    if (!gpu.graph) gpu.graph = std::make_shared<RendererGraphState>();
    RendererGraphState& state = *gpu.graph;
    if (!state.backend) {
        state.backend = std::make_unique<DiligentGraphBackend>(gpu, [this](int scope) { beginPass(static_cast<GpuPass>(scope)); }, [this](int scope) { endPass(static_cast<GpuPass>(scope)); });
        if (const char* path = std::getenv("LIT_RENDER_GRAPH_DUMP"); path && path[0]) {
            state.envDumpPath = path;
            const char* frameText = std::getenv("LIT_RENDER_GRAPH_DUMP_FRAME");
            state.envDumpFrame = frameText ? std::strtoull(frameText, nullptr, 10) : 10;
        }
    }
    rg::RenderGraph& graph = state.graph;
    graph.reset();
    for (int index = 0; index < kMaxPortalViews; ++index) {
        gpu.portalColor[index].Release();
        gpu.portalDepth[index].Release();
        for (auto& target : gpu.portalGBuffer[index]) target.Release();
    }

    FrameGraphData frame;
    frame.camera = &camera;
    frame.scene = &sceneDatabase;
    frame.time = currentFrameTime;
    frame.deltaTime = deltaTime;
    frame.objectCount = objectCount;
    frame.portalsActive = !m_portalFrame.nodes.empty();
    frame.hdrIndex = m_lighting.hdrIndex();
    TransformPlan plan;

    const int slot = static_cast<int>(m_currentFrame);
    frame.depth = graph.importTexture("sceneDepth", Native(gpu.pSceneDepth.RawPtr()));
    for (int target = 0; target < 3; ++target) frame.gbuffer[target] = graph.importTexture(std::format("gbuffer{}", target), Native(m_lighting.gbuffer(target)));
    frame.hdr = graph.importTexture(std::format("hdr{}", frame.hdrIndex), Native(m_lighting.hdrTarget(frame.hdrIndex)));
    frame.hdrOther = graph.importTexture(std::format("hdr{}", 1 - frame.hdrIndex), Native(m_lighting.hdrTarget(1 - frame.hdrIndex)));
    frame.sceneUbo = graph.importBuffer("sceneUniforms", Native(gpu.pSceneUBO[slot].RawPtr()));
    frame.visibilityBits = graph.importBuffer("visibilityBits", Native(gpu.pVisibilityBits.RawPtr()));
    frame.vertices = graph.importBuffer("vertices", Native(gpu.pVBO.RawPtr()));
    frame.indices = graph.importBuffer("indices", Native(gpu.pEBO.RawPtr()));
    frame.objects = graph.importBuffer("objects", Native(gpu.pWorldMatrixBuffer[slot].RawPtr()));
    frame.instances = graph.importBuffer("instancePool", Native(gpu.pInstanceBuffer.RawPtr()));
    static constexpr const char* kPhaseNames[kPhaseCount] = {"opaque", "retest", "transparent", "shadow"};
    for (int phase = 0; phase < kPhaseCount; ++phase) {
        frame.phases[phase] = graph.importBuffer(std::format("visibility/{}", kPhaseNames[phase]), Native(gpu.visibility[phase].drawCommands.RawPtr()));
        frame.meshlets[phase] = graph.importBuffer(std::format("meshlets/{}", kPhaseNames[phase]), Native(gpu.visibility[phase].visibleEntries.RawPtr()));
    }
    frame.accumulation = graph.importTexture("oitAccumulation", Native(gpu.pAccumulation.RawPtr()));
    frame.revealage = graph.importTexture("oitRevealage", Native(gpu.pRevealage.RawPtr()));
    graph.setExternalResolver([this](std::string_view name) { return m_lighting.graphResource(name); });
    graph.setTimingNames(std::vector<std::string>(kGpuPassNames.begin(), kGpuPassNames.end()));
    frame.shadowMap = graph.importTexture("lighting/shadowMap", std::function<void*()>([this] { return Native(m_lighting.shadowMap()); }));
    frame.localShadowMap = graph.importTexture("lighting/localShadowMap", std::function<void*()>([this] { return Native(m_lighting.localShadowMap()); }));
    frame.vsmAtlas = graph.importTexture("lighting/vsmAtlas", std::function<void*()>([this] { return Native(m_lighting.virtualShadowAtlas()); }));

    graph.addPass("Upload", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Upload));
        pass.write(frame.objects, Usage::None);
        pass.sideEffect();
    }, [this, &plan, &sceneDatabase](rg::PassContext&) { plan = uploadSceneData(sceneDatabase); });
    graph.addPass("Instance Upload", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Upload));
        pass.write(frame.instances, Usage::None);
        pass.sideEffect();
    }, [this](rg::PassContext&) { uploadInstances(); });
    graph.addPass("Transforms", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Transform));
        pass.write(frame.objects, Usage::None);
    }, [this, &plan, &sceneDatabase](rg::PassContext&) {
        uploadMeshInfo();
        dispatchTransforms(sceneDatabase, plan);
    });
    graph.addPass("Skinning", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Transform));
        pass.write(frame.vertices, Usage::None);
        pass.write(frame.objects, Usage::None);
    }, [this](rg::PassContext&) { updateDynamicMeshes(); });
    graph.addPass("Frame Setup", [&](rg::PassBuilder& pass) {
        pass.write(frame.sceneUbo, Usage::None);
        pass.sideEffect();
    }, [this, &frame, &camera](rg::PassContext&) {
        auto& state = *m_diligent;
        frame.input = makeFrameInput(camera, frame.time, frame.deltaTime);
        frame.uniforms = SceneUniforms{};
        m_lighting.beginFrame(frame.input, frame.uniforms);
        beginSharedCascades();
        state.mainInput = frame.input;
        LIT_UP(state.pImmediateContext)->UpdateBuffer(state.pSceneUBO[m_currentFrame], 0, sizeof(SceneUniforms), &frame.uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (state.surfaceBindingVersion != m_lighting.bindingVersion()) rebindSurfaceResources();
        syncTextureTable();
    });
    graph.addPass("Custom Before Scene", [&](rg::PassBuilder& pass) { pass.sideEffect(); }, [this](rg::PassContext&) { m_custom.runCompute(ComputeStage::BeforeScene, customFrame()); });
    const std::vector<rg::ExternalAccess> prepare = [&] {
        std::vector<rg::ExternalAccess> externals;
        m_lighting.graphResources(LightingStage::Prepare, externals);
        return externals;
    }();
    graph.addPass("Sky+IBL", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Sky));
        pass.read(frame.sceneUbo, Usage::None);
        pass.access(prepare);
    }, [this](rg::PassContext&) {
        m_lighting.prepare(m_diligent->pSceneUBO[m_currentFrame]);
        m_viewSunShadows = true;
    });

    if (frame.portalsActive) addPortalPasses(graph, frame, objectCount);

    graph.addPass("Cascade Cull Upload", [&](rg::PassBuilder& pass) { pass.write(frame.phases[kPhaseShadow], Usage::None); }, [this](rg::PassContext&) { uploadCascadeCull(); });
    addGeometryPasses(graph, frame, 0, objectCount, nullptr);
    if (frame.portalsActive) addResolvePasses(graph, frame, 0);
    addShadePasses(graph, frame, 0);

    graph.addPass("Custom After Lighting", [&](rg::PassBuilder& pass) {
        pass.write(frame.hdr, Usage::None);
        pass.write(frame.depth, Usage::None);
        pass.sideEffect();
    }, [this](rg::PassContext&) {
        auto& state = *m_diligent;
        CustomFrame lit = customFrame();
        m_custom.runCompute(ComputeStage::AfterLighting, lit);
        std::erase_if(lit.resources, [](const NamedResource& resource) { return resource.name == "u_depth" || resource.name == "u_hdr"; });
        m_custom.runDraws(lit, litgpu::rtv(m_lighting.hdr()), state.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL));
    });
    graph.addPass("Trans Draw", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::TransparentDraw));
        pass.write(frame.accumulation, Usage::RenderTarget);
        pass.write(frame.revealage, Usage::RenderTarget);
        pass.write(frame.depth, Usage::DepthWrite);
        pass.write(frame.hdr, Usage::None);
        pass.read(frame.phases[kPhaseTransparent], Usage::None);
        pass.read(frame.meshlets[kPhaseTransparent], Usage::None);
    }, [this](rg::PassContext&) {
        if (m_diligent->sceneHasTranslucent) m_lighting.captureSceneColor();
        drawTransparency();
    });
    if (frame.portalsActive) {
        graph.addPass("Portal Restore Effects Depth", [&](rg::PassBuilder& pass) {
            pass.read(frame.effectsDepth, Usage::CopySource);
            pass.write(frame.depth, Usage::CopyDest);
        }, [this](rg::PassContext&) { RestorePortalEffectsDepth(*m_diligent); });
    }
    graph.addPass("Custom Before Post", [&](rg::PassBuilder& pass) {
        pass.write(frame.hdr, Usage::None);
        pass.write(frame.hdrOther, Usage::None);
        pass.sideEffect();
    }, [this](rg::PassContext&) {
        const CustomFrame post = customFrame();
        m_custom.runCompute(ComputeStage::BeforePost, post);
        if (!m_custom.hasEffects()) return;
        CustomFrame effects = post;
        std::erase_if(effects.resources, [](const NamedResource& resource) { return resource.name == "u_hdr"; });
        Diligent::ITexture* hdrTargets[2] = {m_lighting.hdrTarget(m_lighting.hdrIndex()), m_lighting.hdrTarget(1 - m_lighting.hdrIndex())};
        if (m_custom.runEffects(effects, hdrTargets, 0) == 1) m_lighting.setHdrIndex(1 - m_lighting.hdrIndex());
    });
    const std::vector<rg::ExternalAccess> post = [&] {
        std::vector<rg::ExternalAccess> externals;
        m_lighting.graphResources(LightingStage::Post, externals);
        return externals;
    }();
    graph.addPass("Post", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Post));
        pass.read(frame.hdr, Usage::None);
        pass.read(frame.hdrOther, Usage::None);
        pass.read(frame.depth, Usage::None);
        pass.access(post);
        pass.sideEffect();
    }, [this, &frame](rg::PassContext&) {
        auto& state = *m_diligent;
        m_lighting.finish(state.pSceneUBO[m_currentFrame], state.pSwapChain->GetCurrentBackBufferRTV(), frame.input.deltaTime, [&](Diligent::ITexture* ldr) {
            if (!m_custom.hasEffects(EffectStage::Ldr)) return;
            CustomFrame effects = customFrame();
            std::erase_if(effects.resources, [](const NamedResource& resource) { return resource.name == "u_hdr"; });
            m_custom.runLdrEffects(effects, ldr);
        });
    });
    graph.addPass("Bind Back Buffer", [&](rg::PassBuilder& pass) {
        pass.read(frame.depth, Usage::None);
        pass.sideEffect();
    }, [this](rg::PassContext&) { bindBackBuffer(); });
    graph.addPass("UI", [&](rg::PassBuilder& pass) {
        pass.timing(static_cast<int>(GpuPass::Ui));
        pass.sideEffect();
    }, [this, &camera](rg::PassContext&) { drawOverlays(camera); });
    graph.addPass("Frame End", [&](rg::PassBuilder& pass) { pass.sideEffect(); }, [this](rg::PassContext&) {
        m_diligent->QueryReady[m_currentFrame] = m_fullProfiling;
        signalFrameFence();
    });

    graph.compile();
    graph.execute(*state.backend, state.pool);
    state.pool.endFrame(*state.backend);
    state.stats = graph.stats();
    ++state.frames;

    std::string dumpPath;
    if (!state.dumpPath.empty()) dumpPath = std::exchange(state.dumpPath, {});
    else if (!state.envDumpPath.empty() && state.frames == state.envDumpFrame) dumpPath = state.envDumpPath;
    if (!dumpPath.empty()) {
        state.lastDump = graph.dumpText();
        WriteText(dumpPath + ".txt", state.lastDump);
        WriteText(dumpPath + ".dot", graph.dumpDot());
        Lit::Log::Info("Render graph dump written to {}.txt and {}.dot ({} passes, {} culled, {} transients in {} physical resources)", dumpPath, dumpPath, state.stats.passes, state.stats.culledPasses, state.stats.allocatedTransients, state.stats.physicalTransients);
    }
    for (int index = 0; index < kMaxPortalViews; ++index) {
        gpu.portalColor[index].Release();
        gpu.portalDepth[index].Release();
        for (auto& target : gpu.portalGBuffer[index]) target.Release();
    }
    gpu.portalDepthScratch.Release();
    gpu.portalEffectsDepth.Release();
}
