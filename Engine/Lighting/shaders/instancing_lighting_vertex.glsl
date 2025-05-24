/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

// Input vertex attributes
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 2) in vec3 vertexNormal;
layout(location = 3) in vec4 vertexColor;
layout(location = 4) in mat4 instanceTransform;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matNormal;

// Output vertex attributes (to fragment shader)
layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out vec3 fragNormal;

void main()
{
    // Compute MVP for current instance
    mat4 mvpi = mvp*instanceTransform;

    // Send vertex attributes to fragment shader
    fragPosition = vec3(mvpi*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));

    // Calculate final vertex position
    gl_Position = mvpi*vec4(vertexPosition, 1.0);
}