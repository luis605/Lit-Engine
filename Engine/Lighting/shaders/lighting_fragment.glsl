#version 460 core

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
//in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse; // Entity Color
uniform vec4 ambientLight;
uniform vec3 viewPos;

// pbr
uniform bool normalMapInit;
uniform sampler2D texture2;

uniform bool roughnessMapInit;
uniform sampler2D texture3;


// Lights
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
};

layout(std140) uniform MaterialBlock {
    SurfaceMaterial surface_material;
};


// Output fragment color
out vec4 finalColor;
out vec3 fragLightDir;
out vec3 fragViewDir;


void main() {
    // Normalize the surface normal
    vec3 norm;
    if (false)
    {
        vec3 normal = texture(texture2, fragTexCoord).xyz * 2.0 - 1.0;
        norm = normalize(normal);
    }
    else
        norm = normalize(fragNormal);

    // float roughness;
    // if (true)
    //     roughness = texture(texture3, fragTexCoord).r;
    // else
    //     roughness = 0.1;


    vec3 lightDir;
    float diff;
    float spec;
    

    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-viewDir, norm);
    
    vec3 diffuse;
    vec3 specular;
    vec3 result = colDiffuse.rgb / 2.0 - ambientLight.rgb * 0.5;
    result += ambientLight.rgb * 0.2;
    
    vec4 texColor;
    if (true)
        texColor = texture(texture0, fragTexCoord);
    else
        texColor = colDiffuse;

    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];
        
        if (light.type == LIGHT_DIRECTIONAL && light.enabled) {
            float diffuseStrength = light.intensity * surface_material.DiffuseIntensity;
            vec3 fragLightDir = normalize(-light.direction);
            float diffuse = diffuseStrength * max(dot(norm, fragLightDir), 0.0);
            vec3 colDiffuse = colDiffuse.rgb * light.color.rgb;
            result += colDiffuse * diffuse;


        } else if (light.type == LIGHT_POINT && light.enabled) {
            lightDir = normalize(light.position - fragPosition);
            
            float distance = length(light.position - fragPosition);
            float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);
            
            diff = max(dot(norm, lightDir), 0.0);
            diffuse = light.intensity * surface_material.DiffuseIntensity * diff * attenuation * (light.color.rgb + vec3(colDiffuse.x, colDiffuse.y, colDiffuse.z)) / 2.0;
            
            result += diffuse;
            
            reflectDir = reflect(-lightDir, norm);
            
            spec = pow(max(dot(viewDir, reflectDir), 0.0), 64.0);
            specular = light.specularStrength * surface_material.SpecularIntensity * spec * attenuation * light.color.rgb;
            specular *= surface_material.SpecularTint;
            
            result += specular;
        }
        else if (light.type == LIGHT_SPOT && light.enabled)
        {
            vec3 fragLightDir = normalize(-light.direction);

            fragViewDir = normalize(viewPos - fragPosition);

            vec3 lightDir = normalize(fragLightDir);
            vec3 viewDir = normalize(fragViewDir);
            
            float distance = length(fragLightDir);
            float attenuation = light.attenuation;
            
            float spotAngle = dot(-lightDir, normalize(light.direction));
            float spotFactor = 100;
            
            float diffuseStrength = 0.7;
            float diffuse = max(dot(norm, lightDir), 0.0);
            
            result += vec3(diffuse * diffuseStrength * light.color * colDiffuse * attenuation * spotFactor);
        }
    }

    vec4 blendedColor = vec4(result, 1.0) * texColor;

    // if (roughnessMapInit)
    // {
    //     float roughnessIntensity = 1.0 - roughness; // Invert roughness value to make roughness=0 fully reflective
    //     blendedColor.rgb *= roughnessIntensity;
    // }


    // Apply gamma correction
    blendedColor.rgb = pow(blendedColor.rgb, vec3(1.0 / 2.2));

    // Assign the final color
    finalColor = blendedColor;

}