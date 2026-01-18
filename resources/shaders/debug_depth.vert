#version 460 core

out gl_PerVertex {
    vec4 gl_Position;
};

layout (location = 0) out vec2 TexCoord;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 texCoords[3] = vec2[](
        vec2(0.0, 1.0),
        vec2(2.0, 1.0),
        vec2(0.0, -1.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    TexCoord = texCoords[gl_VertexIndex];
}
