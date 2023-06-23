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
};


layout(std430, binding = 0) buffer LightsBuffer
{
    Light lights[];
};

uniform int lightsCount;  // Passed from CPU



// Output fragment color
out vec4 finalColor;

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



    for (int i = 0; i < lightsCount; i++) {
        Light light = lights[i];
        
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
    
    finalColor = vec4(result, colDiffuse.a);
    finalColor.rgb = pow(result.rgb, vec3(1.0 / 2.2));
}

