module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Skin;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"
#include "RendererSkinning.hpp"

namespace {

constexpr uint32_t kSkinGroupSize = 64;
constexpr size_t kVertexBytes = kVertexFloats * sizeof(float);
constexpr uint32_t kInitialBoundsCapacity = 16;

struct SkinUniforms {
    uint32_t vertexCount;
    uint32_t morphCount;
    uint32_t skinned;
    uint32_t deformMode;
    glm::vec4 deformA;
    glm::vec4 deformB;
    glm::vec4 deformC;
};

struct CountUniforms {
    uint32_t count;
    uint32_t pad[3];
};

struct UserDeformUniforms {
    uint32_t vertexCount;
    float time;
    uint32_t pad[2];
    glm::vec4 user[4];
};

struct DynamicBoundGpu {
    glm::vec4 sphere;
    uint32_t info[4];
};

uint32_t Groups(uint32_t count) { return (count + kSkinGroupSize - 1) / kSkinGroupSize; }

void UavBarrier(Diligent::IDeviceContext* context, Diligent::IBuffer* buffer) {
    Diligent::StateTransitionDesc barrier;
    barrier.pResource = buffer;
    barrier.OldState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.NewState = Diligent::RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.TransitionType = Diligent::STATE_TRANSITION_TYPE_IMMEDIATE;
    barrier.Flags = Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE;
    context->TransitionResourceStates(1, &barrier);
}

template <typename T>
Diligent::RefCntAutoPtr<Diligent::IBuffer> StructuredFrom(Diligent::IRenderDevice* device, const char* name, const std::vector<T>& data, size_t minimumElements = 1) {
    if (data.size() >= minimumElements && !data.empty()) return CreateStructuredBuffer(device, name, sizeof(T), static_cast<Diligent::Uint32>(data.size()), const_cast<T*>(data.data()));
    std::vector<T> padded(std::max(minimumElements, size_t{1}));
    std::ranges::copy(data, padded.begin());
    return CreateStructuredBuffer(device, name, sizeof(T), static_cast<Diligent::Uint32>(padded.size()), padded.data());
}

bool SameDeform(const DeformSettings& a, const DeformSettings& b) {
    return a.mode == b.mode && a.time == b.time && a.amplitude == b.amplitude && a.frequency == b.frequency && a.speed == b.speed && a.direction == b.direction && a.pinY == b.pinY && a.pinRange == b.pinRange && a.recomputeNormals == b.recomputeNormals && a.userBounds == b.userBounds && a.user == b.user && a.userShader == b.userShader;
}

struct SkinVertexGpu {
    uint32_t joints[4];
    float weights[4];
};

}

DynamicMeshStats Renderer::dynamicStats() const {
    if (!m_diligent || !m_diligent->skinning) return {};
    return m_diligent->skinning->stats;
}

uint32_t Renderer::createDynamicSource(const DynamicMesh& mesh) {
    if (!m_initialized || !m_diligent || !m_diligent->pDevice) return kInvalidMeshId;
    std::string error;
    if (!validateDynamicMesh(mesh, &error)) {
        Lit::Log::Error("Dynamic mesh rejected: {}", error);
        return kInvalidMeshId;
    }
    auto& gpu = *m_diligent;
    if (!gpu.skinning) gpu.skinning = std::make_shared<SkinningGpu>();
    SkinningGpu& skinning = *gpu.skinning;
    if (!skinning.skinPso && !skinning.failed) {
        skinning.skinPso = CreateComputePipeline(gpu.pDevice, "Skin PSO", "resources/shaders/skin.comp");
        skinning.normalsPso = CreateComputePipeline(gpu.pDevice, "Skin Normals PSO", "resources/shaders/skin_normals.comp");
        skinning.boundsPso = CreateComputePipeline(gpu.pDevice, "Skin Bounds PSO", "resources/shaders/skin_bounds.comp", {"WorldMatrixBuffer", "RenderableBuffer", "CullSphereBuffer", "ShaderBoundsBuffer", "DynamicStampBuffer"});
        skinning.boundsUniforms = CreateUniformBuffer(gpu.pDevice, "Skin Bounds Uniforms", sizeof(CountUniforms), Diligent::USAGE_DEFAULT);
        skinning.failed = !skinning.skinPso || !skinning.normalsPso || !skinning.boundsPso;
    }
    if (skinning.failed) return kInvalidMeshId;

    DynamicSourceGpu source;
    source.vertexCount = static_cast<uint32_t>(mesh.vertexCount());
    source.indexCount = static_cast<uint32_t>(mesh.rest.indices.size());
    source.morphCount = static_cast<uint32_t>(mesh.morphs.size());
    source.jointCount = static_cast<uint32_t>(mesh.joints.size());
    source.skinned = mesh.skinned();
    source.bounds = buildSkinBounds(mesh);

    glm::vec3 center(0.0f);
    for (size_t i = 0; i < source.vertexCount; ++i) center += glm::vec3(mesh.rest.vertices[i * kVertexFloats], mesh.rest.vertices[i * kVertexFloats + 1], mesh.rest.vertices[i * kVertexFloats + 2]);
    center /= static_cast<float>(source.vertexCount);
    float radiusSquared = 0.0f;
    for (size_t i = 0; i < source.vertexCount; ++i) radiusSquared = std::max(radiusSquared, glm::distance2(center, glm::vec3(mesh.rest.vertices[i * kVertexFloats], mesh.rest.vertices[i * kVertexFloats + 1], mesh.rest.vertices[i * kVertexFloats + 2])));
    source.restBounds = glm::vec4(center, glm::sqrt(radiusSquared));
    const float silhouette = source.restBounds.w > 0.0f ? std::min(1.0f, std::sqrt(mesh.rest.surfaceArea() / (4.0f * kPi)) / source.restBounds.w) : 1.0f;
    source.packedShape = mesh.rest.isRound() ? -silhouette : silhouette;
    const std::vector<float> samples = mesh.rest.normalSamples(kNormalSamples);
    for (size_t i = 0; i + 3 < samples.size(); i += 4) source.normalSamples.emplace_back(samples[i], samples[i + 1], samples[i + 2], source.packedShape);

    source.rest = CreateStructuredBuffer(gpu.pDevice, "Dynamic Rest Vertices", sizeof(float), static_cast<Diligent::Uint32>(mesh.rest.vertices.size()), const_cast<float*>(mesh.rest.vertices.data()));
    std::vector<SkinVertexGpu> skin;
    if (source.skinned) {
        skin.resize(mesh.skin.size());
        for (size_t i = 0; i < skin.size(); ++i) {
            for (size_t k = 0; k < 4; ++k) {
                skin[i].joints[k] = mesh.skin[i].joints[k];
                skin[i].weights[k] = mesh.skin[i].weights[k];
            }
        }
    }
    source.skin = StructuredFrom(gpu.pDevice, "Dynamic Skin Weights", skin);
    std::vector<glm::vec4> morph;
    morph.reserve(static_cast<size_t>(source.morphCount) * source.vertexCount * 2);
    for (const MorphTarget& target : mesh.morphs) {
        for (size_t i = 0; i < source.vertexCount; ++i) {
            morph.emplace_back(target.positionDelta[i], 0.0f);
            morph.emplace_back(target.normalDelta.empty() ? glm::vec3(0.0f) : target.normalDelta[i], 0.0f);
        }
    }
    source.morph = StructuredFrom(gpu.pDevice, "Dynamic Morph Targets", morph);

    std::vector<uint32_t> offsets(static_cast<size_t>(source.vertexCount) + 1, 0);
    for (unsigned int index : mesh.rest.indices) ++offsets[index + 1];
    for (size_t i = 0; i < source.vertexCount; ++i) offsets[i + 1] += offsets[i];
    std::vector<uint32_t> triangles(mesh.rest.indices.size());
    std::vector<uint32_t> cursor(offsets.begin(), offsets.end() - 1);
    for (size_t i = 0; i < mesh.rest.indices.size(); ++i) triangles[cursor[mesh.rest.indices[i]]++] = static_cast<uint32_t>(i / 3);
    source.adjacencyOffsets = StructuredFrom(gpu.pDevice, "Dynamic Adjacency Offsets", offsets);
    source.adjacencyTriangles = StructuredFrom(gpu.pDevice, "Dynamic Adjacency Triangles", triangles);
    std::vector<uint32_t> indices(mesh.rest.indices.begin(), mesh.rest.indices.end());
    source.indices = StructuredFrom(gpu.pDevice, "Dynamic Indices", indices);

    const size_t indexBytes = indices.size() * sizeof(uint32_t);
    growGeometryBuffers(0, indexBytes);
    LIT_UP(gpu.pImmediateContext)->UpdateBuffer(gpu.pEBO, gTotalIndexBytes, indexBytes, indices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    source.firstIndex = static_cast<uint32_t>(gTotalIndexBytes / sizeof(unsigned int));
    gTotalIndexBytes += indexBytes;

    skinning.sources.push_back(std::move(source));
    return static_cast<uint32_t>(skinning.sources.size() - 1);
}

uint32_t Renderer::createDynamicInstance(uint32_t sourceId) {
    if (!m_initialized || !m_diligent || !m_diligent->skinning || sourceId >= m_diligent->skinning->sources.size()) return kInvalidMeshId;
    auto& gpu = *m_diligent;
    SkinningGpu& skinning = *gpu.skinning;
    DynamicSourceGpu& source = skinning.sources[sourceId];
    while (gMeshInfos.size() % kLodLevelCount != 0) gMeshInfos.push_back(MeshInfo{});
    if (gMeshInfos.size() + kLodLevelCount > kMaxMeshes) {
        Lit::Log::Error("Mesh slot limit of {} reached; refusing to create a dynamic mesh instance.", kMaxMeshes);
        return kInvalidMeshId;
    }

    DynamicInstanceGpu instance;
    instance.source = sourceId;
    const size_t vertexBytes = static_cast<size_t>(source.vertexCount) * kVertexBytes;
    growGeometryBuffers(vertexBytes, 0);
    instance.baseVertex = static_cast<uint32_t>(gTotalVertexBytes / kVertexBytes);
    gTotalVertexBytes += vertexBytes;

    instance.meshId = static_cast<uint32_t>(gMeshInfos.size());
    const MeshInfo info{.indexCount = source.indexCount, .firstIndex = source.firstIndex, .baseVertex = instance.baseVertex, .boundingRadius = source.restBounds.w, .boundingCenter = glm::vec4(glm::vec3(source.restBounds), 1.0f)};
    for (uint32_t level = 0; level < kLodLevelCount; ++level) gMeshInfos.push_back(info);
    if (gNormalSamples.empty()) gNormalSamples.assign(static_cast<size_t>(kMaxMeshes) * kNormalSamples, glm::vec4(0.0f));
    for (size_t i = 0; i < source.normalSamples.size() && i < kNormalSamples; ++i) gNormalSamples[static_cast<size_t>(instance.meshId) * kNormalSamples + i] = source.normalSamples[i];
    m_meshInfoDirty = true;

    instance.palette.assign(std::max<uint32_t>(source.jointCount, 1), glm::mat4(1.0f));
    instance.morph.assign(std::max<uint32_t>(source.morphCount, 1), 0.0f);
    instance.bounds = source.restBounds;
    instance.output = CreateStructuredBuffer(gpu.pDevice, "Dynamic Output Vertices", sizeof(float), source.vertexCount * static_cast<uint32_t>(kVertexFloats));
    instance.paletteBuffer = StructuredFrom(gpu.pDevice, "Dynamic Palette", instance.palette);
    instance.morphWeights = StructuredFrom(gpu.pDevice, "Dynamic Morph Weights", instance.morph);
    instance.skinUniforms = CreateUniformBuffer(gpu.pDevice, "Dynamic Skin Uniforms", sizeof(SkinUniforms), Diligent::USAGE_DEFAULT);
    instance.normalUniforms = CreateUniformBuffer(gpu.pDevice, "Dynamic Normal Uniforms", sizeof(CountUniforms), Diligent::USAGE_DEFAULT);
    CreateBoundSRB(skinning.skinPso, instance.skinSrb,
                   {
                       {"SkinUniforms",      instance.skinUniforms  },
                       {"RestVertexBuffer",  source.rest            },
                       {"SkinVertexBuffer",  source.skin            },
                       {"MorphBuffer",       source.morph           },
                       {"PaletteBuffer",     instance.paletteBuffer },
                       {"MorphWeightBuffer", instance.morphWeights  },
                       {"OutputVertexBuffer", instance.output       }
    });
    CreateBoundSRB(skinning.normalsPso, instance.normalSrb,
                   {
                       {"NormalUniforms",          instance.normalUniforms     },
                       {"AdjacencyOffsetBuffer",   source.adjacencyOffsets     },
                       {"AdjacencyTriangleBuffer", source.adjacencyTriangles   },
                       {"IndexBuffer",             source.indices              },
                       {"OutputVertexBuffer",      instance.output             }
    });
    if (!instance.output || !instance.skinSrb || !instance.normalSrb) return kInvalidMeshId;
    skinning.instances.push_back(std::move(instance));
    return static_cast<uint32_t>(skinning.instances.size() - 1);
}

uint32_t Renderer::dynamicMeshId(uint32_t instance) const {
    if (!m_diligent || !m_diligent->skinning || instance >= m_diligent->skinning->instances.size()) return kInvalidMeshId;
    return m_diligent->skinning->instances[instance].meshId;
}

glm::vec4 Renderer::dynamicBounds(uint32_t instance) const {
    if (!m_diligent || !m_diligent->skinning || instance >= m_diligent->skinning->instances.size()) return glm::vec4(0.0f);
    return m_diligent->skinning->instances[instance].bounds;
}

void Renderer::setDynamicEntity(uint32_t instance, Entity entity) {
    if (!m_diligent || !m_diligent->skinning || instance >= m_diligent->skinning->instances.size()) return;
    m_diligent->skinning->instances[instance].entity = entity;
}

void Renderer::setDynamicPose(uint32_t instance, std::span<const glm::mat4> palette) {
    if (!m_diligent || !m_diligent->skinning || instance >= m_diligent->skinning->instances.size()) return;
    DynamicInstanceGpu& state = m_diligent->skinning->instances[instance];
    const size_t jointCount = state.palette.size();
    bool changed = false;
    for (size_t i = 0; i < jointCount; ++i) {
        const glm::mat4 value = i < palette.size() ? palette[i] : glm::mat4(1.0f);
        if (std::memcmp(&value, &state.palette[i], sizeof(glm::mat4)) != 0) {
            state.palette[i] = value;
            changed = true;
        }
    }
    if (changed) state.dirty = state.boundsDirty = true;
}

void Renderer::setDynamicMorph(uint32_t instance, std::span<const float> weights) {
    if (!m_diligent || !m_diligent->skinning || instance >= m_diligent->skinning->instances.size()) return;
    DynamicInstanceGpu& state = m_diligent->skinning->instances[instance];
    bool changed = false;
    for (size_t i = 0; i < state.morph.size(); ++i) {
        const float value = i < weights.size() ? std::clamp(weights[i], -4.0f, 4.0f) : 0.0f;
        if (value != state.morph[i]) {
            state.morph[i] = value;
            changed = true;
        }
    }
    if (changed) state.dirty = state.boundsDirty = true;
}

void Renderer::setDynamicDeform(uint32_t instance, const DeformSettings& settings) {
    if (!m_diligent || !m_diligent->skinning || instance >= m_diligent->skinning->instances.size()) return;
    DynamicInstanceGpu& state = m_diligent->skinning->instances[instance];
    if (SameDeform(state.deform, settings)) return;
    state.deform = settings;
    state.dirty = state.boundsDirty = true;
}

void Renderer::updateDynamicMeshes() {
    if (!m_diligent || !m_diligent->skinning) return;
    auto& gpu = *m_diligent;
    SkinningGpu& skinning = *gpu.skinning;
    skinning.stats.instances = static_cast<uint32_t>(skinning.instances.size());
    skinning.stats.dispatches = 0;
    skinning.stats.vertices = 0;
    if (skinning.instances.empty() || skinning.failed) return;
    auto* context = gpu.pImmediateContext.RawPtr();

    std::vector<DynamicBoundGpu> boundData;
    for (DynamicInstanceGpu& instance : skinning.instances) {
        const DynamicSourceGpu& source = skinning.sources[instance.source];
        const DeformSettings& deform = instance.deform;
        if (instance.boundsDirty) {
            instance.bounds = computeDynamicBounds(source.bounds, instance.palette, instance.morph, deform);
            instance.boundsDirty = false;
        }
        if (instance.dirty) ++instance.version;
        if (instance.entity != INVALID_ENTITY) boundData.push_back({instance.bounds, {instance.entity, instance.meshId, instance.version, 0}});
        if (!instance.dirty) continue;
        instance.dirty = false;

        const bool user = deform.mode == DeformMode::User && !deform.userShader.empty();
        SkinUniforms uniforms{};
        uniforms.vertexCount = source.vertexCount;
        uniforms.morphCount = source.morphCount;
        uniforms.skinned = source.skinned ? 1u : 0u;
        uniforms.deformMode = static_cast<uint32_t>(user ? DeformMode::None : deform.mode);
        uniforms.deformA = glm::vec4(deform.amplitude, deform.frequency, deform.speed, deform.time);
        uniforms.deformB = glm::vec4(deform.direction, deform.pinY);
        uniforms.deformC = glm::vec4(deform.pinRange, 0.0f, 0.0f, 0.0f);
        LIT_UP(context)->UpdateBuffer(instance.skinUniforms, 0, sizeof(uniforms), &uniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (source.skinned) LIT_UP(context)->UpdateBuffer(instance.paletteBuffer, 0, instance.palette.size() * sizeof(glm::mat4), instance.palette.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (source.morphCount > 0) LIT_UP(context)->UpdateBuffer(instance.morphWeights, 0, instance.morph.size() * sizeof(float), instance.morph.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        context->SetPipelineState(skinning.skinPso);
        context->CommitShaderResources(instance.skinSrb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->DispatchCompute(Diligent::DispatchComputeAttribs(Groups(source.vertexCount), 1, 1));
        ++skinning.stats.dispatches;

        if (user) {
            if (instance.userPath != deform.userShader || !instance.userSrb) {
                auto found = skinning.userPipelines.find(deform.userShader);
                if (found == skinning.userPipelines.end()) found = skinning.userPipelines.emplace(deform.userShader, CreateComputePipeline(gpu.pDevice, "User Deform PSO", deform.userShader)).first;
                instance.userPath = deform.userShader;
                instance.userSrb.Release();
                if (found->second) {
                    if (!instance.userUniforms) instance.userUniforms = CreateUniformBuffer(gpu.pDevice, "User Deform Uniforms", sizeof(UserDeformUniforms), Diligent::USAGE_DEFAULT);
                    CreateBoundSRB(found->second, instance.userSrb, {{"DeformVertices", instance.output}, {"DeformParams", instance.userUniforms}});
                }
            }
            if (instance.userSrb) {
                UserDeformUniforms userUniforms{};
                userUniforms.vertexCount = source.vertexCount;
                userUniforms.time = deform.time;
                for (size_t i = 0; i < 4; ++i) userUniforms.user[i] = deform.user[i];
                LIT_UP(context)->UpdateBuffer(instance.userUniforms, 0, sizeof(userUniforms), &userUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                UavBarrier(context, instance.output);
                context->SetPipelineState(skinning.userPipelines[deform.userShader]);
                context->CommitShaderResources(instance.userSrb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->DispatchCompute(Diligent::DispatchComputeAttribs(Groups(source.vertexCount), 1, 1));
                ++skinning.stats.dispatches;
            }
        }

        bool morphActive = false;
        for (float weight : instance.morph) morphActive = morphActive || weight != 0.0f;
        if (deform.recomputeNormals && (deform.mode != DeformMode::None || morphActive)) {
            const CountUniforms count{source.vertexCount, {0, 0, 0}};
            LIT_UP(context)->UpdateBuffer(instance.normalUniforms, 0, sizeof(count), &count, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            UavBarrier(context, instance.output);
            context->SetPipelineState(skinning.normalsPso);
            context->CommitShaderResources(instance.normalSrb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->DispatchCompute(Diligent::DispatchComputeAttribs(Groups(source.vertexCount), 1, 1));
            ++skinning.stats.dispatches;
        }

        context->CopyBuffer(instance.output, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, gpu.pVBO, static_cast<Diligent::Uint64>(instance.baseVertex) * kVertexBytes, static_cast<Diligent::Uint64>(source.vertexCount) * kVertexBytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        skinning.stats.vertices += source.vertexCount;
    }

    if (boundData.empty()) return;
    const uint32_t count = static_cast<uint32_t>(boundData.size());
    if (!skinning.boundsData || count > skinning.boundsCapacity) {
        skinning.boundsCapacity = std::max({kInitialBoundsCapacity, count, skinning.boundsCapacity * 2});
        skinning.boundsData = CreateStructuredBuffer(gpu.pDevice, "Dynamic Bounds", sizeof(DynamicBoundGpu), skinning.boundsCapacity);
        CreateBoundSRB(skinning.boundsPso, skinning.boundsSrb, {{"BoundsUniforms", skinning.boundsUniforms}, {"DynamicBoundBuffer", skinning.boundsData}});
    }
    if (!skinning.boundsSrb || !gpu.pCullSphereBuffer[m_currentFrame] || !gpu.pWorldMatrixBuffer[m_currentFrame] || !gpu.pRenderableBuffer[m_currentFrame] || !gpu.pShaderBounds) return;
    const CountUniforms countUniforms{count, {0, 0, 0}};
    LIT_UP(context)->UpdateBuffer(skinning.boundsUniforms, 0, sizeof(countUniforms), &countUniforms, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    LIT_UP(context)->UpdateBuffer(skinning.boundsData, 0, boundData.size() * sizeof(DynamicBoundGpu), boundData.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto srv = [](Diligent::IBuffer* buffer) { return buffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE); };
    SetDynamicVariable(skinning.boundsSrb, "WorldMatrixBuffer", srv(gpu.pWorldMatrixBuffer[m_currentFrame]));
    SetDynamicVariable(skinning.boundsSrb, "RenderableBuffer", srv(gpu.pRenderableBuffer[m_currentFrame]));
    SetDynamicVariable(skinning.boundsSrb, "ShaderBoundsBuffer", srv(gpu.pShaderBounds));
    if (!gpu.pDynamicStamps || skinning.stampCapacity != m_maxObjects) {
        gpu.pDynamicStamps = CreateStructuredBuffer(gpu.pDevice, "Dynamic Deform Stamps", sizeof(uint32_t), static_cast<Diligent::Uint32>(std::max<size_t>(m_maxObjects, 1)));
        skinning.stampCapacity = m_maxObjects;
    }
    SetDynamicVariable(skinning.boundsSrb, "DynamicStampBuffer", gpu.pDynamicStamps->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
    SetDynamicVariable(skinning.boundsSrb, "CullSphereBuffer", gpu.pCullSphereBuffer[m_currentFrame]->GetDefaultView(Diligent::BUFFER_VIEW_UNORDERED_ACCESS));
    context->SetPipelineState(skinning.boundsPso);
    context->CommitShaderResources(skinning.boundsSrb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute(Diligent::DispatchComputeAttribs(Groups(count), 1, 1));
    UavBarrier(context, gpu.pCullSphereBuffer[m_currentFrame]);
}
