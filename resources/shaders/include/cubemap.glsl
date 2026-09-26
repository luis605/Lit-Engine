#ifndef LIT_CUBEMAP_GLSL
#define LIT_CUBEMAP_GLSL

vec3 cubeDirection(uint face, vec2 uv) {
    vec2 c = uv * 2.0 - 1.0;
    vec3 d;
    if (face == 0u) d = vec3(1.0, -c.y, -c.x);
    else if (face == 1u) d = vec3(-1.0, -c.y, c.x);
    else if (face == 2u) d = vec3(c.x, 1.0, c.y);
    else if (face == 3u) d = vec3(c.x, -1.0, -c.y);
    else if (face == 4u) d = vec3(c.x, -c.y, 1.0);
    else d = vec3(-c.x, -c.y, -1.0);
    return normalize(d);
}

float cubeTexelSolidAngle(vec2 uv, float size) {
    vec2 c = uv * 2.0 - 1.0;
    float texel = 2.0 / size;
    float d2 = 1.0 + dot(c, c);
    return texel * texel / (d2 * sqrt(d2));
}

#endif
