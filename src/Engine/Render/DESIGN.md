# Design: Render System V0.1

This document specifies the design for a versatile, high-performance, data-oriented render system. It utilizes a flattened hierarchy for transforms and a GPU-driven, multi-pass pipeline for rendering.

# 1. Data Hierarchy (Data-Oriented Design)

The scene is a database of components. An `Entity` is a stable ID (index) into component arrays.

## 1.1 Scene Database
A collection of contiguous component arrays and pre-processed lists.
- `std::vector<TransformComponent> transforms;`
- `std.vector<HierarchyComponent> hierarchies;`
- `std::vector<RenderableComponent> renderables;`
- `std::vector<OpaqueTag> opaqueObjects;` // Tag for opaque objects
- `std::vector<TransparentTag> transparentObjects;` // Tag for transparent objects
- `std::vector<EntityID> sortedHierarchyList;` // The flattened hierarchy, updated on change

## 1.2 Core Components
- **`TransformComponent`**: Contains spatial data.
    - `glm::mat4 localMatrix;` // Transform relative to the parent.
    - `glm::mat4 worldMatrix;` // Final transform in world space. Calculated each frame.
- **`HierarchyComponent`**: Defines entity relationships.
    - `EntityID parent;`
- **`RenderableComponent`**: Defines what to render.
    - `UUID mesh_uuid;`
    - `UUID material_uuid;`
    - `uint32_t shaderId;` // Key for grouping draws.
    - `uint32_t objectId;` // Index into the GPU's master object data buffer.

# 2. Asset Pipeline (Baking)

## 2.1 First Load / On Modification
1.  Load source asset file (.gltf, .fbx, etc.).
2.  Process data into a GPU-ready, engine-native binary format.
    -   Interleave vertex attributes.
    -   Generate mesh and material UUIDs.
    -   Calculate bounding volumes for culling.
3.  Save the baked asset to a cache/asset directory.

## 2.2 Subsequent Loads
1.  Check for a baked asset version.
2.  If it exists, perform a direct memory load of the binary data into the asset manager. CPU processing is minimal.

# 3. Frame Pipeline Overview

The frame is rendered in a sequence of dependent passes. This multi-pass architecture provides both performance for bulk geometry and correctness for complex cases like transparency.

1.  **Transform Update (CPU):** Calculates all world-space transforms.
2.  **Opaque Pass (GPU-Driven):** Renders all opaque geometry with maximum efficiency.
3.  **Transparency Pass (GPU-Driven):** Renders all transparent geometry with correct sorting.

## Stage 1: Transform Update (CPU)

This stage calculates the final world-space position for every object. It runs once per frame.

1.  **Hierarchy Sort (Infrequent)**: If the scene graph has changed, re-calculate the `sortedHierarchyList` via a depth-first traversal.
2.  **Transform Calculation (Per Frame)**: Execute a single, cache-friendly `for` loop over the `sortedHierarchyList` to compute the `worldMatrix` for every entity. This guarantees parents are processed before their children.

## Stage 2: Opaque Pass (Multi-MDI)

This pass renders all opaque geometry using multiple MDI calls, grouped by shader state.

1.  **Buffer Updates**: Update UBOs and SSBOs with the latest camera, lighting, and `worldMatrix` data. Reset GPU atomic counters for each shader bin to zero.
2.  **Culling and Binning (Compute Shader)**: Dispatch a single compute shader that processes all opaque objects. For each visible object, it:
    -   Atomically increments the counter corresponding to its `shaderId`.
    -   Writes a `DrawElementsIndirectCommand` and its instance data into a large buffer, at a position determined by its `shaderId` and the new counter value (a process called "binning").
3.  **Memory Barrier**: Issue a memory barrier to ensure the compute shader's writes are complete.
4.  **Multi-MDI Dispatch (CPU Loop)**: The CPU reads back *only the atomic counters* (a very small data transfer). Then, it loops through the shaders that have visible objects:
    -   `bindShader(shader_id);`
    -   `bindMaterialState(shader_id);`
    -   `glMultiDrawElementsIndirect(..., count_from_counter, offset_of_bin);`
    -   This results in one MDI call per active shader, achieving high performance while maintaining versatility.

## Stage 3: Transparency Pass (GPU-Sorted)

This pass runs after the opaque pass and renders all transparent geometry correctly sorted back-to-front.

1.  **Culling (Compute Shader)**: Dispatch a compute shader that processes all transparent objects.
    -   It performs frustum culling.
    -   For each visible object, it writes its `objectId` and `distance_to_camera` into a new, compact buffer of visible transparent objects.
    -   An atomic counter tracks the total count of visible transparent objects.
2.  **Sorting (Compute Shader)**: Dispatch a second compute shader that executes a parallel sorting algorithm (e.g., Radix Sort) on the buffer of visible transparent objects, sorting them by distance.
3.  **Draw Command Generation (Compute Shader)**: A third, simple compute shader runs over the now-sorted list of objects. It generates a final `DrawCommandBuffer` for these objects.
4.  **Memory Barrier**: Issue a memory barrier to ensure all compute work is finished.
5.  **Single MDI Draw**: Issue a single `glMultiDrawElementsIndirect` call to draw all transparent objects in the correct order using a shader with alpha blending enabled.


# 4. Roadmap

This section outlines a phased implementation plan. Each milestone builds upon the previous one, focusing on delivering a core set of capabilities before moving to more advanced features.

## V0.1: Foundational Core - "The First Triangle"
**Goal:** Prove the viability of the core GPU-driven pipeline by rendering a single, static, opaque mesh.

- **Implement Basic ECS:** Create the core `SceneDatabase` and component structures (`TransformComponent`, `RenderableComponent`).
- **Implement Asset Pipeline:** Build the offline "baking" tool to convert a simple model format (.obj, etc.) into the engine's native binary format.
- **Implement Opaque Pass (Single Shader):** Implement the core of Stage 2. Focus on the compute shader for culling and a single `glMultiDrawElementsIndirect` call for one shader type. Don't implement binning for now.
- **Implement Basic Camera:** A simple camera to view the scene.

## V0.2: A Dynamic Scene - "The Engine Loop"
**Goal:** Expand the core to support a fully dynamic scene with multiple object types and basic lighting.

- **Implement Transform System:** Implement the Flattened Hierarchy sort and the per-frame transform update (Stage 1).
- **Implement Multi-MDI Pipeline:** Enhance the Opaque Pass to support "binning" by `shaderId`, allowing for multiple materials and shaders in the scene.
- **Implement Basic Lighting:** Implement a simple forward lighting model (a single directional light or point light) using the `SceneUBO`.

## V0.3: Modern Visuals - "Fidelity and Performance"
**Goal:** Add key visual and performance features expected in a modern render engine.

- **Implement Transparency Pass:** Implement the full GPU-sorted transparency pipeline (Stage 3).
- **Implement Shadow Mapping:** Re-use the Opaque Pass architecture to render scenes from the perspective of light sources to generate dynamic shadow maps.
- **Implement Automatic LoD:** Integrate the GPU-based Level of Detail selection system into the culling compute shaders.
- **Develop PBR Materials:** Evolve the material system and shaders to support a full Physically Based Rendering (PBR) workflow (Albedo, Metallic, Roughness, AO).

## V0.4: Engine Completeness - "Supporting Systems"
**Goal:** Build the essential rendering systems that exist outside the core 3D world pipeline, making the engine versatile and usable.

- **Implement Post-Processing Stack:** Create a framebuffer system and pipeline for applying full-screen effects like Bloom, SSAO, Tone Mapping, and Color Grading.
- **Implement Sky/Atmosphere Rendering:** Create a dedicated pass for rendering a skybox or a more advanced atmospheric scattering model.
- **Implement Debug Rendering:** Build a simple, immediate-mode style renderer for debug shapes (lines, boxes, spheres) that can be used for visualization and diagnostics.
- **Implement UI Rendering:** Build a dedicated 2D batch renderer for user interface elements, which will run as the final stage in the frame pipeline.
