const char* lightingFrag = R"(
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

uniform bool normalMapReady;
uniform sampler2D texture2;

uniform bool roughnessMapReady;
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
    vec3 relativePosition;
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
    SurfaceMaterial surfaceMaterial;
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

    vec3 fresnel = CalculateFresnelReflection(surfaceMaterial.baseReflectance, viewDir, halfVector);

    vec4 diffuseTerm = colDiffuse * surfaceMaterial.DiffuseIntensity * NdotL;
    vec4 lightContribution = (diffuseTerm + specular) * light.color * vec4(fresnel, 1.0) * light.intensity;

    return lightContribution;
}


vec4 CalculatePointLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec4 specular) {
    vec3 lightDir = normalize(light.position - fragPosition);
    
    // Use viewDir directly without transforming it to world space
    vec3 H = normalize(lightDir + normalize(viewDir));
    
    float distance = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);

    float NdotL = max(dot(norm, lightDir), 0.0) * surfaceMaterial.DiffuseIntensity;

    float NdotH = max(dot(norm, H), 0.0);
    float roughnessSquared = max(1.0 - roughness, 0.02) * surfaceMaterial.Roughness;
    
    // Reuse NdotH for multiple calculations
    float denominator = 4.0 * NdotL * NdotH;
    float k_s = (roughnessSquared + 1.0) / (8.0 * NdotH * NdotL + 0.001);
    
    // Use a fast approximation for pow() for roughnessSquared exponentiation
    float specularTerm = k_s / (NdotH * NdotH * max(denominator, 0.001)) * surfaceMaterial.shininess;

    // Use the unmodified NdotL for the diffuse term
    return (NdotL + specular * specularTerm * surfaceMaterial.SpecularIntensity) * light.color * attenuation * light.intensity;
}





vec4 CalculateSpotLight(Light light, vec3 viewDir, vec3 norm, float roughness, float ao, vec3 fragPosition, vec3 ambient, vec2 texCoord, mat3 TBN) {
    vec3 lightToPoint = light.position - fragPosition;
    float spot = smoothstep(0.6, 0.8, dot(normalize(lightToPoint), light.direction));

    float lightToFragDist = length(light.position - fragPosition);
    float attenuation = 1.0 / (1.0 + light.attenuation * lightToFragDist * lightToFragDist);

    float k_d = 1.0;
    float energyFactor = 1.0 / (k_d + (1.0 - k_d) * 0.5);

    float NdotL;
    if (normalMapReady)
    {
        vec3 normalMap = texture(texture2, texCoord).rgb;
        vec3 sampledNormal = norm;
        vec3 lightDirTangent = normalize(lightToPoint * TBN);
        NdotL = max(dot(sampledNormal, lightDirTangent), 0.0) * surfaceMaterial.DiffuseIntensity;
    }
    else
    {
        vec3 lightDirTangent = normalize(lightToPoint * TBN);
        NdotL = max(dot(norm, lightDirTangent), 0.0) * surfaceMaterial.DiffuseIntensity;
    }

    vec4 diffuseTerm = colDiffuse * NdotL;
    return vec4(diffuseTerm * spot * light.intensity * attenuation * energyFactor) + vec4(colDiffuse.rgb * ambient.rgb, 1);

}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float d = NdotH * NdotH * (a * a - 1.0) + 1.0;
    return a * a / (PI * d * d);
}

// Geometry function for GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
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

    vec3 F = FresnelSchlick(VdotH, vec3(0.04));
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);

    vec3 specular = F * D * G / max(4.0 * NdotL * NdotV, 0.001); // Add epsilon to prevent division by zero

    return (albedo / PI + specular) * NdotL;
}


vec4 CalculateDiffuseLighting(vec3 fragPosition, vec3 norm, vec2 texCoord) {
    vec4 texColor = texture(texture0, texCoord);

    vec3 diffuseComponent = mix(colDiffuse.rgb, texColor.rgb, step(0.0, texColor.rgb));
    vec3 ambientComponent = colDiffuse.rgb * ambientLight.rgb * surfaceMaterial.DiffuseIntensity;
    vec3 resultColor = ambientComponent + diffuseComponent;
    vec4 diffuseColor = vec4(resultColor, texColor.a);

    return diffuseColor;
}

vec4 CalculateLighting(vec3 fragPosition, vec3 fragNormal, vec3 viewDir, vec2 texCoord, SurfaceMaterial material, float roughness) {
    vec4 result = vec4(0.0);

    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];
        
        if (!light.enabled) continue;

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
    

    vec4 diffuseLight = CalculateDiffuseLighting(fragPosition, fragNormal, texCoord);
    result += diffuseLight;
    
    vec4 toneMappedResult = toneMap(result, 0.5);

    float ao = texture(texture4, texCoord).r;
    if (ao != 0.0)
        result *= ao;

    return vec4(toneMappedResult.rgb, result);
}


void main() {
    texCoord = fragTexCoord * tiling;
    vec4 texColor = texture(texture0, texCoord);

    // Calculate tangent space
    vec3 tangent = dFdx(fragPosition);
    vec3 bitangent = dFdy(fragPosition);
    mat3 TBN = mat3(normalize(tangent), normalize(bitangent), normalize(fragNormal));

    // Calculate normal
    vec3 normalMap = normalMapReady ? texture(texture2, texCoord).rgb * 2.0 - 1.0 : vec3(0.0);
    vec3 norm = normalize(normalMapReady ? TBN * normalMap : fragNormal);

    // Calculate roughness
    float roughness = roughnessMapReady ? texture(texture3, texCoord).r : surfaceMaterial.Roughness;

    // Calculate view direction
    vec3 viewDir = normalize(viewPos - fragPosition);

    // Calculate lighting
    vec3 lighting = CalculateLighting(fragPosition, norm, viewDir, texCoord, surfaceMaterial, roughness).rgb;

    // Calculate final color
    vec4 result = vec4(colDiffuse.rgb / 1.8 - ambientLight.rgb * 0.5, colDiffuse.a);
    vec3 ambient = vec3(0);

    finalColor = mix(result, vec4(lighting, colDiffuse.a), step(0.0, length(lighting)));
}
)";


const char* lightingVert = R"(
#version 460 core

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec2 uv;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec2 fragUV;

void main()
{
    // Send vertex attributes to fragment shader
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    
    fragUV = uv;

    // Calculate final vertex position
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

const char* downsamplerFrag = R"(
#version 330 core

const float stepSize = 6.0;

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

const float weightConstant = 0.0007;
float weight = weightConstant * (1.0 + bloomBrightness) * 10;
float attenuation = 1.0 - 6.0 * weight;
vec2 texelSize = vec2(1.0) / textureSize(srcTexture, 0);

uniform int samples = 6;

void main() {
    vec3 result = texture(srcTexture, fragTexCoord).rgb;
    result *= attenuation;

    vec2 offsetStep = texelSize * stepSize;

    for (int i = -samples; i <= samples; i += 3) {
        for (int j = -samples; j <= samples; j += 3) {
            vec2 offset = vec2(i, j) * offsetStep;
            vec2 texCoordOffset = fragTexCoord + offset;

            vec3 tex1 = texture(srcTexture, texCoordOffset).rgb;
            result += tex1 * weight;

            offset += offsetStep; // Reuse offsetStep
            texCoordOffset = fragTexCoord + offset;

            vec3 tex2 = texture(srcTexture, texCoordOffset).rgb;
            result += tex2 * weight;
        }
    }

    FragColor = vec4(result, 1.0);
}
)";


const char* upsamplerFrag = R"(
#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;

void main() {
    vec2 texelSize = 1.0 / textureSize(srcTexture, 0);
    vec4 result = texture(srcTexture, fragTexCoord) + texture(srcTexture, fragTexCoord + vec2(texelSize.x, 0)) +
                  texture(srcTexture, fragTexCoord + vec2(0, texelSize.y)) +
                  texture(srcTexture, fragTexCoord + texelSize);

    FragColor = result / 4.0; // Simple averaging for upscaling
}
)";

const char* skyboxFrag = R"(
#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;

// Input uniform values
uniform samplerCube environmentMap;
uniform bool vflipped;
uniform bool doGamma;

uniform vec4 skyboxColor = vec4(1.0);

// Output fragment color
out vec4 finalColor;

void main()
{
    // Fetch color from texture map
    vec3 color = vec3(0.0);

    if (vflipped) color = texture(environmentMap, vec3(fragPosition.x, -fragPosition.y, fragPosition.z)).rgb;
    else color = texture(environmentMap, fragPosition).rgb;

    color *= skyboxColor.rgb;
    if (doGamma)
    {
        color = color/(color + vec3(1.0));
        color = pow(color, vec3(1.0/2.2));
    }

    // Calculate final fragment color
    finalColor = vec4(color, 1.0);
}
)";


const char* skyboxVert = R"(
#version 330

// Input vertex attributes
in vec3 vertexPosition;

// Input uniform values
uniform mat4 matProjection;
uniform mat4 matView;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;

void main()
{
    // Calculate fragment position based on model transformations
    fragPosition = vertexPosition;

    // Remove translation from the view matrix
    mat4 rotView = mat4(mat3(matView));
    vec4 clipPos = matProjection*rotView*vec4(vertexPosition, 1.0);

    // Calculate final vertex position
    gl_Position = clipPos;
}
)";


const char* cubemapFrag = R"(
#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;

// Input uniform values
uniform sampler2D equirectangularMap;

// Output fragment color
out vec4 finalColor;

vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

void main()
{
    // Normalize local position
    vec2 uv = SampleSphericalMap(normalize(fragPosition));

    // Fetch color from texture map
    vec3 color = texture(equirectangularMap, uv).rgb;

    // Calculate final fragment color
    finalColor = vec4(color, 1.0);
}
)";


const char* cubemapVert = R"(
#version 330

// Input vertex attributes
in vec3 vertexPosition;

// Input uniform values
uniform mat4 matProjection;
uniform mat4 matView;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;

void main()
{
    // Calculate fragment position based on model transformations
    fragPosition = vertexPosition;

    // Calculate final vertex position
    gl_Position = matProjection*matView*vec4(vertexPosition, 1.0);
}
)";

