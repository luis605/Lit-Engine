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

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(viewPos - fragPosition);

    vec3 color;

    // Define the dimensions of the view frustum for cascades
    float nearPlane = 0.1; // Adjust as needed
    float farPlane = 100.0; // Adjust as needed
    float aspectRatio = float(1920) / float(1080); // Example: 1920x1080 resolution

    // Transform fragment position into light space
    vec3 fragPositionLightSpace = fragPosition; // You may need to transform this based on your coordinate system

    // Rotate the fragment position to align with the light direction
    // This rotation should align the light direction with, for example, the Z-axis
    mat3 lightRotationMatrix = mat3(1.0); // Identity matrix, modify as needed
    fragPositionLightSpace = lightRotationMatrix * fragPositionLightSpace;

    // Calculate cascade splits based on the minimum and maximum extents along X and Y
    float cascadeSplitX = max(abs(fragPositionLightSpace.x), nearPlane * tan(radians(45.0) / 2.0));
    float cascadeSplitY = max(abs(fragPositionLightSpace.y), nearPlane * tan(radians(45.0) / 2.0) / aspectRatio);

    // Check if the fragment position is within the cascade splits
    if (fragPositionLightSpace.z >= nearPlane && fragPositionLightSpace.z <= farPlane &&
        abs(fragPositionLightSpace.x) <= cascadeSplitX && abs(fragPositionLightSpace.y) <= cascadeSplitY) {
        color = vec3(1, 0, 1); // Inside the cascade split
    } else {
        color = vec3(0, 1, 0); // Outside the cascade split
    }

    finalColor = vec4(color, 1);
}
