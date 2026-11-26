const uint DEFORM_NONE = 0u;
const uint DEFORM_WAVE = 1u;
const uint DEFORM_CLOTH = 2u;

vec3 deformPosition(uint mode, vec4 a, vec4 b, vec4 c, vec3 position, vec3 normal) {
    if (mode != DEFORM_WAVE && mode != DEFORM_CLOTH) return position;
    float directionLength = length(b.xyz);
    vec3 direction = directionLength > 1e-6 ? b.xyz / directionLength : vec3(1.0, 0.0, 0.0);
    float phase = dot(position, direction) * a.y - a.w * a.z;
    vec3 result = position;
    if (mode == DEFORM_WAVE) {
        result += normal * (a.x * sin(phase));
        result.y += 0.25 * a.x * cos(phase);
    } else {
        float hang = clamp((b.w - position.y) / max(c.x, 1e-4), 0.0, 1.0);
        float amplitude = a.x * hang;
        result += normal * (amplitude * (sin(phase) + 0.5 * sin(2.3 * phase + 1.7)));
    }
    return result;
}
