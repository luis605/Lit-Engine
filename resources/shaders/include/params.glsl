#ifndef LIT_PARAMS_GLSL
#define LIT_PARAMS_GLSL

layout(std430) readonly buffer EngineParams {
    float engineParams[];
};

float litParam(uint slot) { return engineParams[slot]; }

#endif
