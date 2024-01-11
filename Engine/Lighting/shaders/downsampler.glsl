#version 330 core

in vec2 fragTexCoord;
out vec4 FragColor;
uniform sampler2D srcTexture;
uniform float bloomBrightness;

void main() {
    vec3 result = textureLod(srcTexture, fragTexCoord, 0.0).rgb * 0.6364; // Initial sample

    // Combine 10 neighboring samples in a single loop
    for (float i = -5.0; i <= 5.0; i++) {
        vec2 offset = vec2(i) * 0.05; // Assuming texture size is normalized to 1.0
        result += textureLod(srcTexture, fragTexCoord + offset, 0.0).rgb * 0.0909;
    }

    FragColor = vec4(result * bloomBrightness, 1.0);
}
