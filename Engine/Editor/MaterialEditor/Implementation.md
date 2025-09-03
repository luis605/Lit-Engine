All materials are arrays of first-in-last-out layers. A layer is linked to a base definition with parameter overrides, masks, and rules. Editor provides a list of layers, an inspector, painting tools, and a preview viewport.

The Shader Generator creates one shader (and LOD variants) per material by pruning disabled layers and rules, assigning layer indices, declaring uniforms and samplers, inlining base snippets, applying overrides, masks, and blending and then compiling and caching the shader with a hash. Also, the system must remove old caches from old material versions.

A material, masks, and shaders stored in JSON, SVGs or paths or uv coordinates, and binary files respectively under a user-selected directory. All rules compile to GLSL expressions in a way that minimizes branching. Masks can be painted, packed, or procedurally generated.

The runtime can use baked textures for static assets or compiled shaders for dynamic ones. Limits on layers and samplers are enforced.

The worklfwo is create material, then add layers, assign base materials, paint masks or define rules and finally compile to preview.