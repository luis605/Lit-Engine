#version 330 core

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

const float PI = 3.14159265359;
const float MIN_ROUGHNESS = 0.001;
const float EPSILON = 1e-5;

uint bitfieldReverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return bits;
}

vec2 Hammersley(uint i, uint N) {
    return vec2(float(i)/float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
	float a = roughness*roughness;

	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

	vec3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);

	vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}

float GeometrySchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k + EPSILON);
}

float GeometrySmith_IBL(float NdotV_param, float NdotL_param, float roughness_param) {
    float r = max(roughness_param, MIN_ROUGHNESS);
    float k_ibl = (r * r) / 2.0;
    return GeometrySchlickGGX(NdotL_param, k_ibl) * GeometrySchlickGGX(NdotV_param, k_ibl);
}

vec2 IntegrateBRDF(float NdotV_in, float roughness_in) {
    float NdotV = max(NdotV_in, EPSILON);
    float roughness = clamp(roughness_in, MIN_ROUGHNESS, 1.0);

    vec3 V;
    V.x = sqrt(max(0.0, 1.0 - NdotV*NdotV));
    V.y = 0.0;
    V.z = NdotV;

    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0;
    float B = 0.0;

    const uint SAMPLE_COUNT = 1024u;
    for(uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), EPSILON);
        float VdotH = max(dot(V, H), EPSILON);

        if(NdotL > 0.0) {
            float G = GeometrySmith_IBL(NdotV, NdotL, roughness);
            float Vis = G * VdotH / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * Vis;
            B += Fc * Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);

    return vec2(clamp(A, 0.0, 1.0), clamp(B, 0.0, 1.0));
}

void main() {
    vec2 integratedBRDF = IntegrateBRDF(fragTexCoord.x, fragTexCoord.y);
    finalColor = vec4(integratedBRDF, 0, 1);
}