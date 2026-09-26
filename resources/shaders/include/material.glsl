#ifndef LIT_MATERIAL_GLSL
#define LIT_MATERIAL_GLSL

struct MaterialData {
    vec4 baseColorOpacity;
    vec4 emissive;
    vec4 pbr;
    vec4 coat;
    vec4 sheen;
    vec4 subsurface;
    vec4 transmission;
    ivec4 textures;
    vec4 flags;
    vec4 custom0;
    vec4 custom1;
};

layout(std430) readonly buffer MaterialBuffer {
    MaterialData materials[];
};

bool materialTranslucent(uint id) {
    MaterialData m = materials[id];
    return m.baseColorOpacity.w < 0.999 || m.transmission.x > 0.001;
}

#endif
