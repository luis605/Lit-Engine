#version 330 core

in vec2 fragTexCoord;

out vec4 FragColor;
layout (location = 0) out float fragLuminance;

uniform sampler2D srcTexture;

void main() {
    vec3 result = texture(srcTexture, fragTexCoord).rgb;

    fragLuminance = dot(result, vec3(0.2126, 0.7152, 0.0722));
    FragColor = vec4(result, 1.0);
}
