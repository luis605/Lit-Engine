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


#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT 1

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
    bool isChild;
    vec3 direction;
};


layout(std430, binding = 0) buffer LightsBuffer
{
    Light lights[];
};

uniform int lightsCount;  // Passed from CPU

// Output fragment color
out vec4 finalColor;




vec3 directionalLight(vec3 normalizedNormal, vec3 position, vec3 lightPos, vec3 viewPosition, vec3 color, float shadow)
{
    vec3 lightColor = vec3(1.0);
    // Ambient
    vec3 ambient = lightColor * 0.15 * color;
    
    vec3 lightDirection = normalize(lightPos - position);
    vec3 viewDirection = normalize(viewPosition - position);
    vec3 halfwayDirection = normalize(lightDirection + viewDirection);
    
    // Diffuse
    float diffuseIntensity = max(dot(normalizedNormal, lightDirection), 0.0);
    vec3 diffuse = diffuseIntensity * lightColor * color;
    
    // Specular
    float specularStrength = 0.85;
    float specularIntensity = pow(max(dot(normalizedNormal, halfwayDirection), 0.0), 32.0);
    vec3 specular = specularStrength * specularIntensity * lightColor;
    
    // Combine the components and apply shadow
    vec3 result = ambient + (diffuse + specular) * (1.0 - shadow);
    return result;
}

void main() {
    // Normalize the surface normal
    vec3 norm = normalize(fragNormal);
    vec3 lightDir;
    float diff;
    float spec;
    
    // Calculate the view direction from the fragment position to the viewer position
    vec3 viewDir = normalize(viewPos - fragPosition);
    vec3 reflectDir = reflect(-viewDir, norm); // Calculate the reflection direction
    
    vec3 diffuse;
    vec3 specular;
    vec3 result = colDiffuse.rgb / 2.0 - ambientLight.rgb * 0.5;
    result += ambientLight.rgb * 0.2;
    
    vec4 texColor = texture(texture0, fragTexCoord);

    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];
        
        if (light.type == LIGHT_DIRECTIONAL && light.enabled) {
            // Calculate the light position by adding the light direction to the light position
            vec3 lightPos = light.position + light.direction;
            
            result += directionalLight(norm, fragPosition, lightPos, viewPos, colDiffuse.rgb, 0.2);
        }

        else if (light.type == LIGHT_POINT && light.enabled) {
            // Calculate the direction from the fragment position to the light position
            lightDir = normalize(light.position - fragPosition);
            
            float distance = length(light.position - fragPosition);
            float attenuation = 1.0 / (1.0 + light.attenuation * distance * distance);
            
            // Calculate the diffuse component
            diff = max(dot(norm, lightDir), 0.0);
            diffuse = light.intensity * diff * attenuation * (light.color.rgb + vec3(colDiffuse.x, colDiffuse.y, colDiffuse.z)) / 2.0;
            
            // Add the diffuse component to the final result
            result += diffuse;
            
            // Calculate the reflection direction from the light direction and the surface normal
            reflectDir = reflect(-lightDir, norm);
            
            // Calculate the specular component
            spec = pow(max(dot(viewDir, reflectDir), 0.0), 64.0);
            specular = light.specularStrength * spec * attenuation * light.color.rgb;
            
            // Add the specular component to the final result
            result += specular;
        }
    }

    vec4 blendedColor = vec4(result, 1.0) * texColor;
    
    // Apply gamma correction
    blendedColor.rgb = pow(blendedColor.rgb, vec3(1.0 / 2.2));

    // Assign the final color
    finalColor = blendedColor;
}
