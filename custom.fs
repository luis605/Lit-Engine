#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Output fragment color
out vec4 finalColor;

uniform vec3 viewPos;
uniform vec3 lightDirection = normalize(vec3(-1, -1, -1)); // Direction of the light source

// Shadow map samplers
uniform sampler2D cascadeShadowMaps[3];

// Cascade split distances
float cascadeSplitDistances[3] = float[](40.0, 20.0, 10.0);

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);

    float dist = distance(viewPos, fragPosition);

    if (dist > 10)
        finalColor = vec4(1);
    else
        finalColor = vec4(0,0,1,1);
}
