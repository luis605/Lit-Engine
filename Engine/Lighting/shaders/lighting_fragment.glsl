#version 460 core

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Input uniform values
uniform vec4 colDiffuse; // Entity Color
uniform vec4 ambientLight;
uniform vec3 viewPos;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform vec2 tiling;

// pbr
vec2 texCoord;

uniform sampler2D texture0;

uniform bool normalMapInit;
uniform sampler2D texture2;

uniform bool roughnessMapInit;
uniform sampler2D texture3;

uniform sampler2D texture4;

// Lights
#define PI 3.14159265358979323846

#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1
#define LIGHT_SPOT 2

struct Light
{
    int type;
    bool enabled;
    vec3 position;
    vec3 relative_position;
    vec3 target;
    vec4 color;
    float attenuation;
    float intensity;
    float specularStrength;
    float cutOff;
    bool isChild;
    vec3 direction;
};

layout(std430, binding = 0) buffer LightsBuffer
{
    Light lights[];
};

uniform int lightsCount;

// Materials
struct SurfaceMaterial
{
    float shininess;
    float SpecularIntensity;
    float Roughness;
    float DiffuseIntensity;
    vec3 SpecularTint;
    vec3 baseReflectance;
};

layout(std140) uniform MaterialBlock {
    SurfaceMaterial surface_material;
};

// Output fragment color
out vec4 finalColor;
out vec3 fragLightDir;
out vec3 fragViewDir;

// Helper Functions
mat3 CalcTBN(vec3 T, vec3 B, vec3 N)
{
    return mat3(normalize(T), normalize(B), normalize(N));
}

vec3 TangentSpaceNormal(vec3 normalMap, mat3 TBN) {
    return normalize(normalMap * 2.0 - 1.0) * TBN;
}

vec3 CalculateFresnelReflection(vec3 baseReflectance, vec3 viewDirection, vec3 halfVector) {
    float cosTheta = max(dot(viewDirection, halfVector), 0.0);
    float fresnel = pow(1.0 - cosTheta, 5.0);
    return mix(baseReflectance, vec3(1.0), fresnel);
}


vec4 toneMap(vec4 hdrColor, float exposure) {
    vec3 mapped = hdrColor.rgb / (hdrColor.rgb + vec3(1.0));
    mapped = pow(mapped, vec3(1.0 / exposure));
    return vec4(mapped, 1.0);
}


vec4 CalculateDirectionalLight(Light light, vec3 viewDir, vec3 norm, vec3 halfVector, vec4 specular) {
    vec3 fragLightDir = normalize(-light.direction);
    float NdotL = max(dot(norm, fragLightDir), 0.0);

    vec3 fresnel = CalculateFresnelReflection(surface_material.baseReflectance, viewDir, halfVector);

    vec4 diffuseTerm = colDiffuse * surface_material.DiffuseIntensity * NdotL;
    vec4 lightContribution = (diffuseTerm + specular) * light.color * vec4(fresnel, 1.0) * light.intensity;

    return lightContribution;
}


vec4 CalculatePointLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec4 specular) {
    vec3 lightDir = normalize(light.position - fragPosition);
    vec3 viewDirWorld = normalize(vec3(inverse(viewMatrix) * vec4(viewDir, 0.0))); // Transform viewDir to world space
    vec3 H = normalize(lightDir + viewDirWorld); // Calculate H using transformed viewDir
    
    float distance = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);

    float NdotL = max(dot(norm, lightDir), 0.0) * surface_material.DiffuseIntensity;

    float NdotH = max(dot(norm, H), 0.0);
    float roughnessFactor = max(1.0 - roughness, 0.02) * surface_material.Roughness;
    float roughnessSquared = roughnessFactor * roughnessFactor;
    float geometricTerm = 2.0 * NdotH / (NdotH + NdotL);
    float k_s = (roughnessSquared + 1.0) / (8.0 * NdotH * NdotL + 0.001);
    float k_d = 1.0 - k_s;

    // Calculate the specular term
    float specularTerm = k_s / (NdotH * NdotH * max(4.0 * NdotL * NdotH, 0.001)) * surface_material.shininess;

    // Use the unmodified NdotL for the diffuse term
    return (NdotL + specular * specularTerm * surface_material.SpecularIntensity) * light.color * attenuation * light.intensity;
}


vec4 CalculateSpotLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec3 ambient, vec2 texCoord, mat3 TBN) {
    vec3 lightToPoint = light.position - fragPosition;
    float spot = smoothstep(0.6, 0.8, dot(normalize(lightToPoint), light.direction)); // Adjust the thresholds for smoother transition

    float lightToFragDist = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);

    // Energy conservation: Scale down diffuse term
    float k_d = 1.0; // Adjust this value to control energy conservation
    float energyFactor = 1.0 / (k_d + (1.0 - k_d) * 0.5);

    // Apply normal mapping
    float NdotL;
    if (false)
    {
        vec3 normalMap = texture(texture2, texCoord).rgb;
        vec3 sampledNormal = normalize((normalMap * 2.0 - 1.0) * TBN);
        vec3 lightDirTangent = normalize(lightToPoint * TBN);
        NdotL = max(dot(sampledNormal, lightDirTangent), 0.0) * surface_material.DiffuseIntensity;
    }
    else
    {
        // Calculate the light direction in tangent space
        vec3 lightDirTangent = normalize(lightToPoint * TBN);

        // Calculate the diffuse term using the sampled normal
        NdotL = max(dot(norm, lightDirTangent), 0.0) * surface_material.DiffuseIntensity;
    }


    vec4 diffuseTerm = colDiffuse * NdotL;

    return vec4(diffuseTerm * spot * light.intensity * attenuation * energyFactor) + vec4(colDiffuse.rgb * ambient.rgb, 1);

}

// Helper function to calculate the Cook-Torrance F term
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Distribution function for GGX
float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d);
}

// Geometry function for GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Cook-Torrance BRDF
vec3 CookTorrance(vec3 L, vec3 V, vec3 N, vec3 albedo, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = vec3(0.04); // F0 for dielectrics
    vec3 F = FresnelSchlick(VdotH, F0);
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
    
    vec3 specular = F * D * G / (4.0 * NdotL * NdotV + 0.001); // Add epsilon to prevent division by zero
    
    return (albedo / PI + specular) * NdotL;
}


vec4 CalculateDiffuseLighting(vec3 fragPosition, vec3 norm, vec2 texCoord) {
    // Retrieve the color from the texture
    vec4 texColor = texture(texture0, texCoord);

    // Check if the texture is not valid or transparent
    if (texColor.rgb == vec3(0.0)) {
        return colDiffuse * ambientLight * surface_material.DiffuseIntensity;
    }

    // Calculate the ambient component
    vec3 ambientComponent = colDiffuse.rgb * ambientLight.rgb * surface_material.DiffuseIntensity;

    // Use the texture color for the diffuse component
    vec3 diffuseComponent = texColor.rgb;

    // Combine ambient and diffuse components
    vec3 resultColor = ambientComponent + (diffuseComponent * colDiffuse.rgb);

    // Multiply by alpha (transparency)
    vec4 diffuseColor = vec4(resultColor, texColor.a);

    return diffuseColor;
}



// Function to calculate the final lighting
vec4 CalculateLighting(vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec2 texCoord, SurfaceMaterial material, float roughness) {
    vec4 result = vec4(0.0);

    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];
        
        // Calculate light contributions (directional, point, and spot)
        if (light.enabled) {
            if (light.type == LIGHT_DIRECTIONAL) {
                // Calculate directional light
                result += CalculateDirectionalLight(light, viewDir, fragNormal, vec3(0.0), vec4(0.0));
            } else if (light.type == LIGHT_POINT) {
                // Calculate point light
                result += CalculatePointLight(light, viewDir, fragNormal, material.Roughness, 1.0, fragPosition, vec4(0.0));
            } else if (light.type == LIGHT_SPOT) {
                // Calculate spot light
                result += CalculateSpotLight(light, viewDir, fragNormal, material.Roughness, 1.0, fragPosition, vec3(0.0), texCoord, mat3(1.0));
            }
            vec3 specular = CookTorrance(fragLightDir, viewDir, fragNormal, colDiffuse.rgb, roughness);
            specular = max(specular, vec3(0.0)); // Ensure all components are non-negative
            result += vec4(specular, 1);

        }
    }
    

    vec4 diffuseLight = CalculateDiffuseLighting(fragPosition, fragNormal, texCoord);
    result += diffuseLight;
    
    vec4 toneMappedResult = toneMap(result, 0.5);

    float ao = texture(texture4, texCoord).r;
    if (ao != 0.0)
        result *= ao;

    // return vec4(toneMappedResult.rgb, result);
    return result;
}


void main() {
    texCoord = fragTexCoord * tiling;
    vec4 texColor = texture(texture0, texCoord);

    // Calculate tangent space
    vec3 tangent = dFdx(fragPosition);
    vec3 bitangent = dFdy(fragPosition);
    vec3 T = normalize(tangent);
    vec3 B = normalize(bitangent);
    vec3 N = normalize(fragNormal);
    mat3 TBN = mat3(T, B, N);

    // Calculate normal
    vec3 norm;
    if (normalMapInit) {
        vec3 normalMap = texture(texture2, texCoord).rgb;
        normalMap = normalMap * 2.0 - 1.0;
        norm = normalize(TBN * normalMap);
    } else {
        norm = N;
    }

    // Calculate material properties
    float roughness;
    if (roughnessMapInit) {
        roughness = texture(texture3, texCoord).r;
    } else {
        roughness = surface_material.Roughness;
    }

    // Calculate view direction
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-viewDir, norm);

    vec4 result = colDiffuse / 1.8 - ambientLight * 0.5;
    vec3 ambient = vec3(0);
    
    // Calculate lighting
    finalColor = vec4(CalculateLighting(fragPosition, norm, viewDir, texCoord, surface_material, roughness).rgb, colDiffuse.a);
}