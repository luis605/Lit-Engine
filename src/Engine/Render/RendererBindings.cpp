module;

#include "RendererPrelude.hpp"

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
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> MakeSurfaceBinding(DiligentData& gpu, Lighting& lighting, Diligent::IPipelineState* pipeline, int frameIndex, Diligent::IBuffer* visibleObjects) {
    return litgpu::makeBinding(pipeline,
                               {
                                   {"SceneData",             gpu.pSceneUBO[frameIndex]               },
                                   {"MaterialBuffer",        lighting.materialBuffer()             },
                                   {"WorldMatrixBuffer",     gpu.pWorldMatrixBuffer[frameIndex]      },
                                   {"CullSphereBuffer",      gpu.pCullSphereBuffer[frameIndex]       },
                                   {"CullOrientationBuffer", gpu.pCullOrientationBuffer[frameIndex]  },
                                   {"NormalSampleBuffer",    gpu.pNormalSampleBuffer                 },
                                   {"VisibleObjectBuffer",   visibleObjects                          },
                                   {"AlphaBuffer",           gpu.pAlphaBuffer                        },
                                   {"ExposureBuffer",        lighting.exposureBuffer()             },
                                   {"LightBuffer",           lighting.lightBuffer()                },
                                   {"ClusterBuffer",         lighting.clusterBuffer()              },
                                   {"IrradianceBuffer",      lighting.irradianceBuffer()           },
                                   {"u_colorTextures",       lighting.colorTextures()              },
                                   {"u_dataTextures",        lighting.dataTextures()               },
                                   {"u_prefilteredEnv",      lighting.prefilteredEnvironment()     },
                                   {"u_dfgLut",              lighting.dfgLut()                     },
                                   {"u_shadowMap",           litgpu::srv(lighting.shadowMap())     },
                                   {"u_shadowDepth",         litgpu::srv(lighting.shadowMap())     },
                                   {"u_localShadowMap",      litgpu::srv(lighting.localShadowMap())},
                                   {"u_sceneColor",          lighting.sceneColor()                 }
    });
}

Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> MakeShadowBinding(DiligentData& gpu, Lighting& lighting, Diligent::IPipelineState* pipeline, int frameIndex, int view) {
    return litgpu::makeBinding(pipeline,
                               {
                                   {"SceneData",             gpu.pSceneUBO[frameIndex]                 },
                                   {"MaterialBuffer",        lighting.materialBuffer()               },
                                   {"WorldMatrixBuffer",     gpu.pWorldMatrixBuffer[frameIndex]        },
                                   {"CullSphereBuffer",      gpu.pCullSphereBuffer[frameIndex]         },
                                   {"CullOrientationBuffer", gpu.pCullOrientationBuffer[frameIndex]    },
                                   {"NormalSampleBuffer",    gpu.pNormalSampleBuffer                   },
                                   {"VisibleObjectBuffer",   gpu.visibility[kPhaseShadow].sortedEntries},
                                   {"ShadowPass",            gpu.pShadowPassUBO[view]                  }
    });
}

}

void Renderer::bindComputeResources(int frameIndex) {
    auto& gpu = *m_diligent;
    CreateBoundSRB(gpu.pTransformPSO, gpu.pTransformSRB[frameIndex],
                   {
                       {"TransformUniforms",     gpu.pTransformUniforms                },
                       {"TransformBuffer",       gpu.pObjectBuffer[frameIndex]         },
                       {"WorldMatrixBuffer",     gpu.pWorldMatrixBuffer[frameIndex]    },
                       {"HierarchyBuffer",       gpu.pHierarchyBuffer[frameIndex]      },
                       {"SortedHierarchyBuffer", gpu.pSortedHierarchyBuffer[frameIndex]},
                       {"TouchedEpochBuffer",    gpu.pTouchedEpochBuffer[frameIndex]   },
                       {"CullOrientationBuffer", gpu.pCullOrientationBuffer[frameIndex]},
                       {"CullSphereBuffer",      gpu.pCullSphereBuffer[frameIndex]     },
                       {"RenderableBuffer",      gpu.pRenderableBuffer[frameIndex]     },
                       {"MeshInfoBuffer",        gpu.pMeshInfoBuffer                   },
                       {"BasePositionBuffer",    gpu.pBasePositionBuffer               },
                       {"ShaderBoundsBuffer",    gpu.pShaderBounds                     }
    });
    CreateBoundSRB(gpu.pTransformPSO, gpu.pTransformWorkSRB[frameIndex],
                   {
                       {"TransformUniforms",     gpu.pTransformUniforms                },
                       {"TransformBuffer",       gpu.pObjectBuffer[frameIndex]         },
                       {"WorldMatrixBuffer",     gpu.pWorldMatrixBuffer[frameIndex]    },
                       {"HierarchyBuffer",       gpu.pHierarchyBuffer[frameIndex]      },
                       {"SortedHierarchyBuffer", gpu.pTransformWorkBuffer              },
                       {"TouchedEpochBuffer",    gpu.pTouchedEpochBuffer[frameIndex]   },
                       {"CullOrientationBuffer", gpu.pCullOrientationBuffer[frameIndex]},
                       {"CullSphereBuffer",      gpu.pCullSphereBuffer[frameIndex]     },
                       {"RenderableBuffer",      gpu.pRenderableBuffer[frameIndex]     },
                       {"MeshInfoBuffer",        gpu.pMeshInfoBuffer                   },
                       {"BasePositionBuffer",    gpu.pBasePositionBuffer               },
                       {"ShaderBoundsBuffer",    gpu.pShaderBounds                     }
    });
    CreateBoundSRB(gpu.pApplyDirtyPSO, gpu.pApplyDirtySRB[frameIndex],
                   {
                       {"ApplyDirtyConstants", gpu.pApplyDirtyConstants           },
                       {"DirtyIndexBuffer",    gpu.pDirtyIndexBuffer[frameIndex]  },
                       {"DirtyPayloadBuffer",  gpu.pDirtyPayloadBuffer[frameIndex]},
                       {"TransformBuffer",     gpu.pObjectBuffer[frameIndex]      },
                       {"TouchedEpochBuffer",  gpu.pTouchedEpochBuffer[frameIndex]}
    });
}

void Renderer::bindDispatchArgsResources(int frameIndex) {
    auto& gpu = *m_diligent;
    CreateBoundSRB(gpu.pDispatchArgsPSO, gpu.pTransparentDispatchArgsSRB[frameIndex],
                   {
                       {"DispatchArgsConstants", gpu.pObjectDispatchArgsConstants},
                       {"SourceCounterBuffer",   gpu.pTransparentCandidateCounter},
                       {"DispatchArgsBuffer",    gpu.pTransparentDispatchArgs    }
    });
    CreateBoundSRB(gpu.pDispatchArgsPSO, gpu.pCasterDispatchArgsSRB[frameIndex],
                   {
                       {"DispatchArgsConstants", gpu.pObjectDispatchArgsConstants},
                       {"SourceCounterBuffer",   gpu.pCasterCounter              },
                       {"DispatchArgsBuffer",    gpu.pCasterDispatchArgs         }
    });
    CreateBoundSRB(gpu.pDispatchArgsPSO, gpu.pLocalCasterDispatchArgsSRB[frameIndex],
                   {
                       {"DispatchArgsConstants", gpu.pObjectDispatchArgsConstants},
                       {"SourceCounterBuffer",   gpu.pLocalCasterCounter         },
                       {"DispatchArgsBuffer",    gpu.pLocalCasterDispatchArgs    }
    });
    CreateBoundSRB(gpu.pDispatchArgsPSO, gpu.pShadowRetestDispatchArgsSRB[frameIndex],
                   {
                       {"DispatchArgsConstants", gpu.pObjectDispatchArgsConstants},
                       {"SourceCounterBuffer",   gpu.pShadowRetestCounter        },
                       {"DispatchArgsBuffer",    gpu.pShadowRetestDispatchArgs   }
    });
    CreateBoundSRB(gpu.pDispatchArgsPSO, gpu.pEarlyDispatchArgsSRB[frameIndex],
                   {
                       {"DispatchArgsConstants", gpu.pObjectDispatchArgsConstants},
                       {"SourceCounterBuffer",   gpu.pEarlyCandidateCounter      },
                       {"DispatchArgsBuffer",    gpu.pEarlyCandidateDispatchArgs }
    });
    CreateBoundSRB(gpu.pVisibilityCompactPSO, gpu.pEarlyCompactSRB[frameIndex],
                   {
                       {"CullingUniforms",        gpu.visibility[kPhaseOpaque].cullingUniforms},
                       {"VisibilityBuffer",       gpu.pVisibilityBits                         },
                       {"CandidateCounterBuffer", gpu.pEarlyCandidateCounter                  },
                       {"CandidateBuffer",        gpu.pEarlyCandidateIds                      }
    });
}

void Renderer::bindVisibilityResources(int frameIndex) {
    auto& gpu = *m_diligent;
    bindDispatchArgsResources(frameIndex);
    for (int phaseIndex = kPhaseOpaque; phaseIndex < kPhaseCount; ++phaseIndex) {
        auto& phase = gpu.visibility[phaseIndex];
        if (phaseIndex != kPhaseShadow) {
            Diligent::IPipelineState* cullPipeline = phaseIndex == kPhaseOpaque ? gpu.pCullingPSO.RawPtr() : phaseIndex == kPhaseRetest ? gpu.pRetestCullingPSO.RawPtr() : gpu.pTransparentCullPSO.RawPtr();
            const bool isOpaque = phaseIndex == kPhaseOpaque;
            CreateBoundSRB(cullPipeline, phase.cullSRB[frameIndex],
                           {
                               {"SceneData",                gpu.pSceneUBO[frameIndex]                                                   },
                               {"CullingUniforms",          phase.cullingUniforms                                                       },
                               {"AtomicCounterBuffer",      phase.visibleCounter                                                        },
                               {"VisibleObjectBuffer",      phase.visibleEntries                                                        },
                               {"CullSphereBuffer",         gpu.pCullSphereBuffer[frameIndex]                                           },
                               {"RenderableBuffer",         gpu.pRenderableBuffer[frameIndex]                                           },
                               {"BucketCountBuffer",        phase.bucketCounts                                                          },
                               {"VisibilityBuffer",         gpu.pVisibilityBits                                                         },
                               {"TransparentCounterBuffer", gpu.pTransparentCandidateCounter                                            },
                               {"TransparentBuffer",        gpu.pTransparentCandidateIds                                                },
                               {"CandidateCounterBuffer",   isOpaque ? gpu.pEarlyCandidateCounter : gpu.pTransparentCandidateCounter    },
                               {"CandidateBuffer",          isOpaque ? gpu.pEarlyCandidateIds : gpu.pTransparentCandidateIds            },
                               {"MaterialBuffer",           m_lighting.materialBuffer()                                                 },
                               {"CascadeCull",              gpu.pCascadeCullUBO                                                         },
                               {"CasterCounterBuffer",      gpu.pCasterCounter                                                          },
                               {"CasterBuffer",             gpu.pCasterIds                                                              },
                               {"LocalCasterCounterBuffer", gpu.pLocalCasterCounter                                                     },
                               {"LocalCasterBuffer",        gpu.pLocalCasterIds                                                         }
            });
        }
        CreateBoundSRB(gpu.pPrefixSumPSO, phase.prefixSumSRB[frameIndex],
                       {
                           {"PrefixSumConstants",    gpu.pPrefixSumConstants},
                           {"BucketCountBuffer",     phase.bucketCounts     },
                           {"BucketOffsetBuffer",    phase.bucketOffsets    },
                           {"BucketWriteHeadBuffer", phase.bucketWriteHeads }
        });
        CreateBoundSRB(gpu.pDispatchArgsPSO, phase.scatterDispatchArgsSRB[frameIndex],
                       {
                           {"DispatchArgsConstants", gpu.pObjectDispatchArgsConstants},
                           {"SourceCounterBuffer",   phase.visibleCounter            },
                           {"DispatchArgsBuffer",    phase.scatterDispatchArgs       }
        });
        CreateBoundSRB(gpu.pScatterPSO, phase.scatterSRB[frameIndex],
                       {
                           {"ScatterConstants",          gpu.pScatterConstants},
                           {"AtomicCounterBuffer",       phase.visibleCounter },
                           {"VisibleObjectBuffer",       phase.visibleEntries },
                           {"BucketWriteHeadBuffer",     phase.bucketWriteHeads},
                           {"SortedVisibleObjectBuffer", phase.sortedEntries  }
        });
        CreateBoundSRB(gpu.pCommandGenPSO, phase.commandGenSRB[frameIndex],
                       {
                           {"CommandGenConstants",     gpu.pCommandGenConstants},
                           {"PointCounterBuffer",      phase.pointCounters     },
                           {"PointCommandBuffer",      phase.pointCommands     },
                           {"DrawAtomicCounterBuffer", phase.drawCounters      },
                           {"DrawCommandBuffer",       phase.drawCommands      },
                           {"MeshInfoBuffer",          gpu.pMeshInfoBuffer     },
                           {"BucketCountBuffer",       phase.bucketCounts      },
                           {"BucketOffsetBuffer",      phase.bucketOffsets     }
        });
    }
}

void Renderer::bindShadowResources(int frameIndex) {
    auto& gpu = *m_diligent;
    auto& phase = gpu.visibility[kPhaseShadow];
    for (int cascade = 0; cascade < kCascadeShadowViews; ++cascade) {
        CreateBoundSRB(gpu.pShadowCullPSO, gpu.shadowCullSRB[frameIndex][cascade],
                       {
                           {"SceneData",                 gpu.pSceneUBO[frameIndex]         },
                           {"CullingUniforms",           phase.cullingUniforms             },
                           {"AtomicCounterBuffer",       phase.visibleCounter              },
                           {"VisibleObjectBuffer",       phase.visibleEntries              },
                           {"CullSphereBuffer",          gpu.pCullSphereBuffer[frameIndex] },
                           {"RenderableBuffer",          gpu.pRenderableBuffer[frameIndex] },
                           {"BucketCountBuffer",         phase.bucketCounts                },
                           {"MaterialBuffer",            m_lighting.materialBuffer()       },
                           {"ShadowCull",                gpu.pShadowCullUBO[cascade]       },
                           {"CandidateCounterBuffer",    gpu.pCasterCounter                },
                           {"CandidateBuffer",           gpu.pCasterIds                    },
                           {"ShadowRetestCounterBuffer", gpu.pShadowRetestCounter          },
                           {"ShadowRetestBuffer",        gpu.pShadowRetestIds              }
        });
        CreateBoundSRB(gpu.pShadowLateCullPSO, gpu.shadowLateCullSRB[frameIndex][cascade],
                       {
                           {"SceneData",              gpu.pSceneUBO[frameIndex]         },
                           {"CullingUniforms",        phase.cullingUniforms             },
                           {"AtomicCounterBuffer",    phase.visibleCounter              },
                           {"VisibleObjectBuffer",    phase.visibleEntries              },
                           {"CullSphereBuffer",       gpu.pCullSphereBuffer[frameIndex] },
                           {"RenderableBuffer",       gpu.pRenderableBuffer[frameIndex] },
                           {"BucketCountBuffer",      phase.bucketCounts                },
                           {"MaterialBuffer",         m_lighting.materialBuffer()       },
                           {"ShadowCull",             gpu.pShadowCullUBO[cascade]       },
                           {"CandidateCounterBuffer", gpu.pShadowRetestCounter          },
                           {"CandidateBuffer",        gpu.pShadowRetestIds              }
        });
    }
    CreateBoundSRB(gpu.pLocalShadowCullPSO, gpu.localCullSRB[frameIndex],
                   {
                       {"SceneData",              gpu.pSceneUBO[frameIndex]                       },
                       {"CullingUniforms",        phase.cullingUniforms                           },
                       {"AtomicCounterBuffer",    phase.visibleCounter                            },
                       {"VisibleObjectBuffer",    phase.visibleEntries                            },
                       {"CullSphereBuffer",       gpu.pCullSphereBuffer[frameIndex]               },
                       {"RenderableBuffer",       gpu.pRenderableBuffer[frameIndex]               },
                       {"BucketCountBuffer",      phase.bucketCounts                              },
                       {"MaterialBuffer",         m_lighting.materialBuffer()                     },
                       {"ShadowCull",             gpu.pShadowCullUBO[kCascadeShadowViews]         },
                       {"LocalShadowViews",       gpu.pLocalShadowViewsUBO                        },
                       {"CandidateCounterBuffer", gpu.pLocalCasterCounter                         },
                       {"CandidateBuffer",        gpu.pLocalCasterIds                             }
    });
}

void Renderer::rebindSurfaceResources() {
    auto& gpu = *m_diligent;
    gpu.surfaceBindingVersion = m_lighting.bindingVersion();
    for (int frameIndex = 0; frameIndex < NUM_FRAMES_IN_FLIGHT; ++frameIndex) {
        for (int phaseIndex = kPhaseOpaque; phaseIndex <= kPhaseTransparent; ++phaseIndex) {
            auto& phase = gpu.visibility[phaseIndex];
            const auto meshPipelines = SurfacePipelines(gpu, phaseIndex, false);
            const auto pointPipelines = SurfacePipelines(gpu, phaseIndex, true);
            phase.opaqueSRBs[frameIndex].assign(meshPipelines.size(), {});
            phase.pointSRBs[frameIndex].assign(pointPipelines.size(), {});
            for (size_t surfaceIndex = 0; surfaceIndex < meshPipelines.size(); ++surfaceIndex) phase.opaqueSRBs[frameIndex][surfaceIndex] = MakeSurfaceBinding(gpu, m_lighting, meshPipelines[surfaceIndex], frameIndex, phase.sortedEntries);
            for (size_t surfaceIndex = 0; surfaceIndex < pointPipelines.size(); ++surfaceIndex) phase.pointSRBs[frameIndex][surfaceIndex] = MakeSurfaceBinding(gpu, m_lighting, pointPipelines[surfaceIndex], frameIndex, phase.sortedEntries);
        }
        const auto shadowMeshes = SurfacePipelines(gpu, kPhaseShadow, false);
        const auto shadowPoints = SurfacePipelines(gpu, kPhaseShadow, true);
        for (int view = 0; view < kShadowViews; ++view) {
            gpu.shadowSRBs[frameIndex][view].assign(shadowMeshes.size(), {});
            gpu.shadowPointSRBs[frameIndex][view].assign(shadowPoints.size(), {});
            for (size_t surfaceIndex = 0; surfaceIndex < shadowMeshes.size(); ++surfaceIndex) gpu.shadowSRBs[frameIndex][view][surfaceIndex] = MakeShadowBinding(gpu, m_lighting, shadowMeshes[surfaceIndex], frameIndex, view);
            for (size_t surfaceIndex = 0; surfaceIndex < shadowPoints.size(); ++surfaceIndex) gpu.shadowPointSRBs[frameIndex][view][surfaceIndex] = MakeShadowBinding(gpu, m_lighting, shadowPoints[surfaceIndex], frameIndex, view);
        }
    }
}
