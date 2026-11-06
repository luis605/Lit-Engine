module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

module Engine.Render.lighting;

import Engine.glm;
import Engine.Render.settings;
import Engine.Render.material;

#include "LightingInternal.hpp"

using namespace lighting_detail;

namespace {

constexpr float kHistogramMinLog = -10.0f;
constexpr float kHistogramMaxLog = 22.0f;
constexpr float kMaxExposureDeltaTime = 0.25f;
constexpr uint32_t kHistogramDownscale = 4;
constexpr uint32_t kHistogramGroupSize = 16;
constexpr size_t kExposureEvOffset = sizeof(float) * 3;
constexpr int kGodRayPassCount = 3;

uint32_t groupCount(uint32_t size) { return (size + kThreadGroupSize - 1) / kThreadGroupSize; }

void createPostBindings(Lighting::Data& state, Diligent::IBuffer* scene) {
    for (int parity = 0; parity < 2; ++parity) {
        state.histogramSrb[parity] = litgpu::makeBinding(state.histogramPso,
                                                         {
                                                             {"u_hdr",             litgpu::srv(state.hdrTargets[parity])},
                                                             {"ExposureBuffer",    state.exposure                       },
                                                             {"HistogramBuffer",   state.histogram                      },
                                                             {"ExposureConstants", state.exposureConstants              }
        });
        state.bloomSourceSrb[parity] = litgpu::makeBinding(state.bloomDownPso,
                                                           {
                                                               {"u_source",       litgpu::srv(state.hdrTargets[parity])},
                                                               {"u_output",       litgpu::uav(state.bloomDown[0])      },
                                                               {"BloomConstants", state.bloomConstants                 }
        });
        state.tonemapSrb[parity] = litgpu::makeBinding(state.tonemapPso,
                                                       {
                                                           {"SceneData",      scene                                },
                                                           {"u_hdr",          litgpu::srv(state.hdrTargets[parity])},
                                                           {"u_bloom",        litgpu::srv(state.bloomUp[0])        },
                                                           {"u_depth",        litgpu::srv(state.shadeDepth)        },
                                                           {"u_gbuffer2",     litgpu::srv(state.gbuffer[2])        },
                                                           {"ExposureBuffer", state.exposure                       }
        });
    }
    state.exposureSrb = litgpu::makeBinding(state.exposurePso,
                                            {
                                                {"ExposureBuffer",    state.exposure         },
                                                {"HistogramBuffer",   state.histogram        },
                                                {"ExposureConstants", state.exposureConstants}
    });
    state.commitSrb = litgpu::makeBinding(state.commitPso,
                                          {
                                              {"ExposureBuffer", state.exposure}
    });
    state.bloomDownSrbs.clear();
    state.bloomUpSrbs.clear();
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        if (level == 0) {
            state.bloomDownSrbs.emplace_back();
            continue;
        }
        state.bloomDownSrbs.push_back(litgpu::makeBinding(state.bloomDownPso,
                                                          {
                                                              {"u_source",       litgpu::srv(state.bloomDown[level - 1])},
                                                              {"u_output",       litgpu::uav(state.bloomDown[level])    },
                                                              {"BloomConstants", state.bloomConstants                   }
        }));
    }
    for (uint32_t level = 0; level + 1 < kBloomLevels; ++level) {
        Diligent::ITexture* lower = level + 2 == kBloomLevels ? state.bloomDown[level + 1].RawPtr() : state.bloomUp[level + 1].RawPtr();
        state.bloomUpSrbs.push_back(litgpu::makeBinding(state.bloomUpPso,
                                                        {
                                                            {"u_lower",        litgpu::srv(lower)                 },
                                                            {"u_current",      litgpu::srv(state.bloomDown[level])},
                                                            {"u_output",       litgpu::uav(state.bloomUp[level])  },
                                                            {"BloomConstants", state.bloomConstants               }
        }));
    }
    state.fxaaSrb = litgpu::makeBinding(state.fxaaPso,
                                        {
                                            {"SceneData", scene                                },
                                            {"u_ldr",     litgpu::srv(finalColorTexture(state))}
    });
    createUpscaleBindings(state, scene);
}

void createGodRayBindings(Lighting::Data& state, Diligent::IBuffer* scene) {
    state.godRayVersion = state.bindingVersion;
    state.godRayDepth = state.shadeDepth;
    state.godRayMaskSrb = litgpu::makeBinding(state.godRayMaskPso,
                                              {
                                                  {"SceneData",       scene                        },
                                                  {"GodRayConstants", state.godRayConstants        },
                                                  {"u_depth",         litgpu::srv(state.shadeDepth)},
                                                  {"u_clouds",        litgpu::srv(state.clouds)    },
                                                  {"u_output",        litgpu::uav(state.godRays[0])}
    });
    for (int index = 0; index < 2; ++index) {
        state.godRayBlurSrb[index] = litgpu::makeBinding(state.godRayBlurPso,
                                                         {
                                                             {"SceneData",       scene                                },
                                                             {"GodRayConstants", state.godRayConstants                },
                                                             {"u_input",         litgpu::srv(state.godRays[index])    },
                                                             {"u_output",        litgpu::uav(state.godRays[1 - index])}
        });
        state.godRayCompositeSrb[index] = litgpu::makeBinding(state.godRayCompositePso,
                                                              {
                                                                  {"SceneData",       scene                               },
                                                                  {"GodRayConstants", state.godRayConstants               },
                                                                  {"u_shafts",        litgpu::srv(state.godRays[1])       },
                                                                  {"u_hdr",           litgpu::uav(state.hdrTargets[index])},
                                                                  {"ExposureBuffer",  state.exposure                      }
        });
    }
}

void renderGodRays(Lighting::Data& state, Diligent::IBuffer* scene, int parity) {
    const RenderSettings& settings = state.settings;
    const bool enabled = settings.godRays && settings.godRayIntensity > 0.0f && settings.sky == SkyMode::Physical && state.shadeDepth && state.godRayMaskPso && state.godRayBlurPso && state.godRayCompositePso;
    if (!enabled) return;
    if (state.godRayVersion != state.bindingVersion || state.godRayDepth != state.shadeDepth || !state.godRayMaskSrb) createGodRayBindings(state, scene);
    Diligent::IDeviceContext* context = state.context;
    const auto& rayDesc = state.godRays[0]->GetDesc();
    const uint32_t groupsX = groupCount(rayDesc.Width);
    const uint32_t groupsY = groupCount(rayDesc.Height);
    litgpu::dispatch(context, state.godRayMaskPso, state.godRayMaskSrb, groupsX, groupsY);
    const glm::vec4 passes[kGodRayPassCount] = {glm::vec4(1.0f, 0.04f, 0.0f, 0.0f), glm::vec4(0.1f, 0.0f, 0.0f, 0.0f), glm::vec4(0.01f, 0.0f, 0.0f, 0.0f)};
    int source = 0;
    for (const glm::vec4& pass : passes) {
        context->UpdateBuffer(state.godRayConstants, 0, sizeof(pass), &pass, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        litgpu::dispatch(context, state.godRayBlurPso, state.godRayBlurSrb[source], groupsX, groupsY);
        source = 1 - source;
    }
    litgpu::dispatch(context, state.godRayCompositePso, state.godRayCompositeSrb[parity], groupCount(static_cast<uint32_t>(state.width)), groupCount(static_cast<uint32_t>(state.height)));
}

void updateExposure(Lighting::Data& state, float deltaTime, int parity) {
    const RenderSettings& settings = state.settings;
    Diligent::IDeviceContext* context = state.context;
    const ExposureConstants constants{glm::vec4(kHistogramMinLog, 1.0f / (kHistogramMaxLog - kHistogramMinLog), settings.minEV100, settings.maxEV100), glm::vec4(settings.autoExposure ? 1.0f : 0.0f, settings.manualEV100, std::clamp(deltaTime, 0.0f, kMaxExposureDeltaTime), settings.exposureCompensation - state.nightDarkening), glm::vec4(settings.adaptationUp, settings.adaptationDown, 0.5f, 0.98f)};
    context->UpdateBuffer(state.exposureConstants, 0, sizeof(constants), &constants, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (state.snapExposure) {
        const float unset = 0.0f;
        context->UpdateBuffer(state.exposure, kExposureEvOffset, sizeof(float), &unset, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        state.snapExposure = false;
    }
    const uint32_t groupsX = (static_cast<uint32_t>(state.width) / kHistogramDownscale + kHistogramGroupSize - 1) / kHistogramGroupSize;
    const uint32_t groupsY = (static_cast<uint32_t>(state.height) / kHistogramDownscale + kHistogramGroupSize - 1) / kHistogramGroupSize;
    litgpu::dispatch(context, state.histogramPso, state.histogramSrb[parity], groupsX, groupsY);
    litgpu::dispatch(context, state.exposurePso, state.exposureSrb, 1);
}

void renderBloom(Lighting::Data& state, int parity) {
    Diligent::IDeviceContext* context = state.context;
    const float radius = state.settings.bloomRadius;
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        const glm::vec4 params(level == 0 ? 1.0f : 0.0f, radius, 0.0f, 0.0f);
        context->UpdateBuffer(state.bloomConstants, 0, sizeof(params), &params, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto& desc = state.bloomDown[level]->GetDesc();
        Diligent::IShaderResourceBinding* binding = level == 0 ? state.bloomSourceSrb[parity].RawPtr() : state.bloomDownSrbs[level].RawPtr();
        litgpu::dispatch(context, state.bloomDownPso, binding, groupCount(desc.Width), groupCount(desc.Height));
    }
    const glm::vec4 upParams(0.0f, radius, 0.0f, 0.0f);
    context->UpdateBuffer(state.bloomConstants, 0, sizeof(upParams), &upParams, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for (int level = static_cast<int>(kBloomLevels) - 2; level >= 0; --level) {
        const auto& desc = state.bloomUp[level]->GetDesc();
        litgpu::dispatch(context, state.bloomUpPso, state.bloomUpSrbs[static_cast<size_t>(level)], groupCount(desc.Width), groupCount(desc.Height));
    }
}

void drawFullscreen(Diligent::IDeviceContext* context, Diligent::IPipelineState* pso, Diligent::IShaderResourceBinding* binding) {
    context->SetPipelineState(pso);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE});
}

}  // namespace

Diligent::ITextureView* Lighting::sceneColor() const { return litgpu::srv(m_data->sceneColor); }
Diligent::ITexture* Lighting::sceneColorTexture() const { return m_data->sceneColor; }
bool Lighting::pyramidValid() const { return m_data->pyramidValid; }
void Lighting::setPyramidValid(bool valid) { m_data->pyramidValid = valid; }

void Lighting::snapExposure() { m_data->snapExposure = true; }

void Lighting::finish(Diligent::IBuffer* sceneUniforms, Diligent::ITextureView* backBuffer, float deltaTime, const std::function<void(Diligent::ITexture*)>& afterTonemap) {
    Data& state = *m_data;
    Diligent::IDeviceContext* context = state.context;
    if (state.finishVersion != state.bindingVersion || !state.tonemapSrb[0] || state.tonemapDepth != state.shadeDepth) {
        state.finishVersion = state.bindingVersion;
        state.tonemapDepth = state.shadeDepth;
        createPostBindings(state, sceneUniforms);
    }
    const int parity = state.hdrIndex;
    renderGodRays(state, sceneUniforms, parity);
    updateExposure(state, deltaTime, parity);
    if (state.settings.bloom) renderBloom(state, parity);

    Diligent::ITextureView* ldrTarget = litgpu::rtv(state.ldr);
    context->SetRenderTargets(1, &ldrTarget, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(state.width);
    viewport.Height = static_cast<float>(state.height);
    const auto targetWidth = static_cast<Diligent::Uint32>(state.width);
    const auto targetHeight = static_cast<Diligent::Uint32>(state.height);
    context->SetViewports(1, &viewport, targetWidth, targetHeight);
    if (state.tonemapPso && state.tonemapSrb[parity]) drawFullscreen(context, state.tonemapPso, state.tonemapSrb[parity]);
    litgpu::dispatch(context, state.commitPso, state.commitSrb, 1);
    if (afterTonemap) afterTonemap(state.ldr);
    runUpscale(state);
    context->SetRenderTargets(1, &backBuffer, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    viewport.Width = static_cast<float>(state.outputWidth);
    viewport.Height = static_cast<float>(state.outputHeight);
    context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(state.outputWidth), static_cast<Diligent::Uint32>(state.outputHeight));
    if (state.fxaaPso && state.fxaaSrb) drawFullscreen(context, state.fxaaPso, state.fxaaSrb);
    state.hdrIndex = 1 - state.hdrIndex;
}
