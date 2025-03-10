#version 460 core

in vec3 vertexPosition;
in vec2 vertexTexCoord;

uniform mat4 matProjection;
uniform mat4 matView;
uniform mat4 matModel;

out vec3 fragPosition;
out vec2 fragTexCoord;

void main() {
    fragPosition = vertexPosition;
    fragTexCoord = vertexTexCoord;

    mat4 rotView = mat4(mat3(matView));
    vec4 worldPosition = matModel * vec4(vertexPosition, 1.0);
    gl_Position = matProjection * rotView * worldPosition;
}
