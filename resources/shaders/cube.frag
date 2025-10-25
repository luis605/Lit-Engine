#version 330 core
out vec4 FragColor;

uniform vec3 u_colorA;
uniform vec3 u_colorB;

void main() {
    float blendFactor = gl_FragCoord.y / 600.0;
    vec3 finalColor = mix(u_colorA, u_colorB, blendFactor);
    FragColor = vec4(finalColor, 1.0f);
}