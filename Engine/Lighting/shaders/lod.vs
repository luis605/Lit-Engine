#version 330 core

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

// Custom variables
uniform float distanceThreshold = 3.0; // Distance at which to switch LODs
uniform vec3 viewPos;
uniform float popDistance = 1.0; // Distance by which to pop vertices

void main()
{
    // Calculate the distance from the camera
    float distanceToCamera = length(vertexPosition - viewPos);

    // Calculate LOD factor based on distance
    float lodFactor = distanceToCamera / distanceThreshold;

    // If LOD factor is below a threshold, pop the vertices
    if (lodFactor <= 10.0) {
        // Calculate the vector from the vertex to the camera
        vec3 toCamera = viewPos - vertexPosition;
        
        // Normalize and scale the vector to achieve the popping effect
        vec3 poppedPosition = vertexPosition + normalize(toCamera) * popDistance;
        vec4 poppedMVP = mvp * vec4(poppedPosition, 1.0);

        // Send popped attributes to the fragment shader
        fragPosition = poppedPosition;
        fragTexCoord = vertexTexCoord;
        fragColor = vertexColor;

        mat3 normalMatrix = transpose(inverse(mat3(matModel)));
        fragNormal = normalize(normalMatrix * vertexNormal);

        // Calculate final vertex position
        gl_Position = poppedMVP;
    }
    else {
        return;
    }
}
