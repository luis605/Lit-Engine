const uint RENDER_HIDDEN = 1u;
const uint RENDER_NO_OCCLUDER = 2u;
const uint RENDER_TRANSLUCENT = 4u;

layout(std430) readonly buffer RenderableBuffer {
    uint renderables[];
};

uint renderableMesh(uint renderable) { return renderable & 0xFFFu; }
uint renderableMaterial(uint renderable) { return (renderable >> 12u) & 0x3FFu; }
uint renderableShader(uint renderable) { return (renderable >> 22u) & 0xFu; }
uint renderableFlags(uint renderable) { return renderable >> 26u; }
