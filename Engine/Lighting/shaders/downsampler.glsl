#version 330 core

// This shader performs downsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.
// This particular method was customly designed to eliminate
// "pulsating artifacts and temporal stability issues".

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
uniform sampler2D srcTexture;
uniform vec2 srcResolution;

in vec2 fragTexCoord;

out vec4 finalColor;

void main()
{
    vec2 srcTexelSize = 1.0 / srcResolution;
    float x = srcTexelSize.x;
    float y = srcTexelSize.y;

    // Take 13 samples around current texel:
    vec3 a = texture(srcTexture, fragTexCoord + vec2(-2.0*x, 2.0*y)).rgb;
    vec3 b = texture(srcTexture, fragTexCoord + vec2(0.0, 2.0*y)).rgb;
    vec3 c = texture(srcTexture, fragTexCoord + vec2(2.0*x, 2.0*y)).rgb;
    vec3 d = texture(srcTexture, fragTexCoord + vec2(-2.0*x, 0.0)).rgb;
    vec3 e = texture(srcTexture, fragTexCoord).rgb;
    vec3 f = texture(srcTexture, fragTexCoord + vec2(2.0*x, 0.0)).rgb;
    vec3 g = texture(srcTexture, fragTexCoord + vec2(-2.0*x, -2.0*y)).rgb;
    vec3 h = texture(srcTexture, fragTexCoord + vec2(0.0, -2.0*y)).rgb;
    vec3 i = texture(srcTexture, fragTexCoord + vec2(2.0*x, -2.0*y)).rgb;
    vec3 j = texture(srcTexture, fragTexCoord + vec2(-x, y)).rgb;
    vec3 k = texture(srcTexture, fragTexCoord + vec2(x, y)).rgb;
    vec3 l = texture(srcTexture, fragTexCoord + vec2(-x, -y)).rgb;
    vec3 m = texture(srcTexture, fragTexCoord + vec2(x, -y)).rgb;

    // Apply weighted distribution:
    finalColor = vec4(e * 0.5, 1.0); // Center pixel
    finalColor += vec4((a + b + c + d + f + g + h + i) * 0.03125, 1.0); // Surrounding pixels
    finalColor += vec4((j + k + l + m) * 0.125, 1.0); // Diagonal pixels
    finalColor = max(finalColor, 0.0001); // Clamping

    // Optionally, you can add tonemapping or other post-processing effects here
}
