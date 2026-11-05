#pragma once

struct HiZSet {
    Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> source[kMaxHiZMips];
    Diligent::RefCntAutoPtr<Diligent::ITextureView> dest[kMaxHiZMips];
};

struct PortalHistory {
    uint64_t key = 0;
    uint64_t lastFrame = 0;
    bool used = false;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> bits;
    HiZSet hiz[2];
    int previous = 0;
};

struct DiligentData {
    Diligent::Uint32 renderWidth = 0;
    Diligent::Uint32 renderHeight = 0;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> pDevice;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> pImmediateContext;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> pSwapChain;
    Diligent::IEngineFactoryVk* pFactoryVk = nullptr;

    static constexpr int NumFrames = 3;
    Diligent::RefCntAutoPtr<Diligent::IFence> pFences[NumFrames];
    Diligent::Uint64 FenceValues[NumFrames] = {0};
    Diligent::Uint64 CurrentFenceValue = 0;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pObjectBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pWorldMatrixBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCullSphereBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCullOrientationBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDirtyIndexBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDirtyPayloadBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTouchedEpochBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pHierarchyBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pRenderableBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSortedHierarchyBuffer[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransformWorkBuffer;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransformWorkSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pSceneUBO[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pMeshInfoBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pNormalSampleBuffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransformPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransformSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransformUniforms;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pBasePositionBuffer;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVBO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pEBO;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pHiZTextures[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::ISampler> pHiZSampler;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pHiZMipViewsSource[NumFrames][kMaxHiZMips];
    Diligent::RefCntAutoPtr<Diligent::ITextureView> pHiZMipViewsDest[NumFrames][kMaxHiZMips];

    bool QueryReady[NumFrames] = {false};

    struct VisibilityPhase {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> cullingUniforms;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> visibleCounter;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> visibleEntries;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> sortedEntries;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> bucketCounts;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> bucketOffsets;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> bucketWriteHeads;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> scatterDispatchArgs;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> drawCommands;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> drawCounters;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pointCommands;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> pointCounters;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> cullSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> prefixSumSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> scatterDispatchArgsSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> scatterSRB[NumFrames];
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> commandGenSRB[NumFrames];
        std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> opaqueSRBs[NumFrames];
        std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> pointSRBs[NumFrames];
    };
    VisibilityPhase visibility[kPhaseCount];
    struct SurfaceProgram {
        std::string name;
        std::string path;
        float bounds = 0.0f;
        bool twoSided = false;
        bool valid = false;
        std::string error;
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> gbuffer;
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> gbufferPoint;
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> forward;
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> forwardPoint;
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadow;
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> shadowPoint;
    };
    std::vector<SurfaceProgram> surfaces;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pShadowCullPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShaderBounds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowCullUBO[kShadowViews];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowPassUBO[kShadowViews];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pLocalShadowCullPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> localCullSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLocalShadowViewsUBO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLocalCasterCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLocalCasterIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pLocalCasterDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pLocalCasterDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCascadeCullUBO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCasterCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCasterIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCasterDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCasterDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pShadowLateCullPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadowLateCullSRB[NumFrames][kCascadeShadowViews];
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pShadowHiZPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pShadowHiZSRB;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pShadowHiZArgsPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pShadowHiZArgsSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowHiZArgs;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowHiZArgsConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowRetestCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowRetestIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pShadowRetestDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pShadowRetestDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pEarlyCandidateCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pEarlyCandidateIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pEarlyCandidateDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pEarlyCompactSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pEarlyDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::ITexture> shadowHiZ[kShadowLayers];
    std::vector<Diligent::RefCntAutoPtr<Diligent::ITextureView>> shadowHiZDest[kShadowLayers];
    std::vector<Diligent::RefCntAutoPtr<Diligent::ITextureView>> shadowHiZSource[kShadowLayers];
    Diligent::RefCntAutoPtr<Diligent::ITextureView> shadowSlices[kShadowLayers];
    Diligent::ITexture* shadowHiZSourceMap = nullptr;
    glm::mat4 shadowHiZViewProj[kShadowLayers]{};
    bool shadowHiZValid[kShadowLayers] = {};
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> shadowCullSRB[NumFrames][kCascadeShadowViews];
    std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> shadowSRBs[NumFrames][kShadowViews];
    std::vector<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>> shadowPointSRBs[NumFrames][kShadowViews];
    uint64_t surfaceBindingVersion = 0;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCandidateCounter;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentCandidateIds;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pTransparentDispatchArgs;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pTransparentDispatchArgsSRB[NumFrames];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pAlphaBuffer;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pAccumulation;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pRevealage;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCompositePSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pCompositeSRB;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pRetestCullingPSO;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pVisibilityBits;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pObjectDispatchArgsConstants;

    Diligent::RefCntAutoPtr<Diligent::ITexture> pSceneDepth;
    Diligent::RefCntAutoPtr<Diligent::ITexture> pOverlayDepth;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZFromDepthPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZFromDepthSRB;

    Diligent::RefCntAutoPtr<Diligent::IQuery> passQueries[kGpuPassCount][NumFrames][2];
    bool passRecorded[NumFrames][kGpuPassCount] = {};
    double passMilliseconds[kGpuPassCount] = {};
    double gpuTotalMilliseconds = 0.0;
    double fenceWaitMilliseconds = 0.0;
    float profileLogTimer = 0.0f;

    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pHiZMipmapSRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pApplyDirtySRB[NumFrames];

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pHiZMipmapPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCullingPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pVisibilityCompactPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pCommandGenPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPrefixSumPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pScatterPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pApplyDirtyPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pTransparentCullPSO;

    Diligent::RefCntAutoPtr<Diligent::IBuffer> pCommandGenConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPrefixSumConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pScatterConstants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pApplyDirtyConstants;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugDepthPSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDepthUpscalePSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDepthUpscaleSRB;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugLinePSO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDebugLineOverlayPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDebugLineOverlaySRB;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDebugLineSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDebugLineVB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDebugLineUBO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pDebugDepthSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDebugDepthUniforms;

    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pDispatchArgsPSO;
    ObservationPass observation;
    bool observationTried = false;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pDispatchArgsConstants;

    std::vector<PortalHistory> portalHistory;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> portalMainBits;
    FrameInput mainInput;
    Diligent::RefCntAutoPtr<Diligent::ITexture> portalColor[kMaxPortalViews];
    Diligent::RefCntAutoPtr<Diligent::ITexture> portalDepth[kMaxPortalViews];
    Diligent::RefCntAutoPtr<Diligent::ITexture> portalDepthScratch;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPortalDepthPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pPortalDepthSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPortalDepthUBO;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pPortalPSO;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> pPortalSRB;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> pPortalUBO;
    Diligent::RefCntAutoPtr<Diligent::IQuery> portalQueries[NumFrames][kMaxPortalViews][2];
    Diligent::RefCntAutoPtr<Diligent::IBuffer> portalStaging[NumFrames][kMaxPortalViews];
    struct PortalSnapshot {
        std::string label;
        int depth = 0;
        bool recorded = false;
        bool timed = false;
    };
    PortalSnapshot portalSnapshot[NumFrames][kMaxPortalViews];
};
