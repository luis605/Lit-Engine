#version 330 core

// This shader performs upsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
uniform sampler2D srcTexture;
uniform float filterRadius;

in vec2 fragTexCoord;
out vec4 finalColor;

void main()
{
    // The filter kernel is applied with a radius, specified in texture
    // coordinates, so that the radius will vary across mip resolutions.
    float x = filterRadius;
    float y = filterRadius;

    // Take 13 samples around the current texel:
    vec3 a = texture(srcTexture, fragTexCoord + vec2(-x, 2.0 * y)).rgb;
    vec3 b = texture(srcTexture, fragTexCoord + vec2(0.0, 2.0 * y)).rgb;
    vec3 c = texture(srcTexture, fragTexCoord + vec2(x, 2.0 * y)).rgb;
    vec3 d = texture(srcTexture, fragTexCoord + vec2(-x, y)).rgb;
    vec3 e = texture(srcTexture, fragTexCoord + vec2(0.0, y)).rgb;
    vec3 f = texture(srcTexture, fragTexCoord + vec2(x, y)).rgb;
    vec3 g = texture(srcTexture, fragTexCoord + vec2(-x, 0.0)).rgb;
    vec3 h = texture(srcTexture, fragTexCoord + vec2(0.0, 0.0)).rgb;
    vec3 i = texture(srcTexture, fragTexCoord + vec2(x, 0.0)).rgb;
    vec3 j = texture(srcTexture, fragTexCoord + vec2(-x, -y)).rgb;
    vec3 k = texture(srcTexture, fragTexCoord + vec2(0.0, -y)).rgb;
    vec3 l = texture(srcTexture, fragTexCoord + vec2(x, -y)).rgb;
    vec3 m = texture(srcTexture, fragTexCoord + vec2(-x, -2.0 * y)).rgb;
    vec3 n = texture(srcTexture, fragTexCoord + vec2(0.0, -2.0 * y)).rgb;
    vec3 o = texture(srcTexture, fragTexCoord + vec2(x, -2.0 * y)).rgb;

    // Apply a larger weighted distribution:
    finalColor = vec4(e * 4.0, 1.0); // Center pixel
    finalColor += vec4((a + b + c + d + f + g + h + i + j + k + l + m + n + o) * 0.05, 1.0); // Surrounding pixels
    finalColor *= vec4(vec3(1.0 / 4.16), 1); // Normalize the result

    // Optionally, you can add tonemapping or other post-processing effects here
}
