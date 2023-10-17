#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Output fragment color
out vec4 finalColor;

uniform vec3 viewPos;
uniform vec3 lightDirection; // Direction of the light source

// Define your cascade splits here, e.g., for three cascades
uniform float cascadeSplits[4]; // Four splits for three cascades

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);

    float dist = distance(viewPos, fragPosition);

    // Calculate which cascade this fragment belongs to based on distance
    int cascadeIndex = 0;
    for (int i = 0; i < 3; i++) {
        if (dist >= cascadeSplits[i]) {
            cascadeIndex = i + 1;
        }
    }

    // Perform shadow mapping for the corresponding cascade using the cascadeIndex
    // You'll need to sample the shadow map and determine if this fragment is in shadow
    // and use the appropriate color accordingly.

    // Pseudo-code for shadow mapping:
    // float shadowValue = SampleShadowMap(cascadeIndex, fragPosition);

    vec3 color;

    // Based on shadowValue, you can adjust the final color
    if (shadowValue < 0.5) {
        color = vec3(0, 0, 1); // In shadow
    } else {
        color = vec3(1, 1, 1); // In light
    }

    finalColor = vec4(color, 1);
}
