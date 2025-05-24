/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;

uniform mat4 matProjection;
uniform mat4 matView;
uniform mat4 matModel;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    fragPosition = vertexPosition;
    fragTexCoord = vertexTexCoord;

    mat4 rotView = mat4(mat3(matView));
    vec4 worldPosition = matModel * vec4(vertexPosition, 1.0);
    gl_Position = matProjection * rotView * worldPosition;
}
