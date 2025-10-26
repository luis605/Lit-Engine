#version 460 core

out vec4 FragColor;

void main() {
    float n = fract(sin(gl_PrimitiveID * 12.9898) * 43758.5453);
    vec3 color = vec3(fract(n * 1.1), fract(n * 2.2), fract(n * 3.3));
    FragColor = vec4(color, 1.0);
}
