#version 330 core
out vec4 FragColor;

in vec2 fragTexCoord;

uniform sampler2D screenTexture;
uniform bool horizontal;

uniform float weight[11] = {
    0.09, 0.12, 0.15, 0.18, 0.20,
    0.18, 0.15, 0.12, 0.09, 0.06,
    0.03
};

void main()
{
    vec2 tex_offset = 3.0 / textureSize(screenTexture, 0); // Gets the size of a single texel
    vec3 result = texture(screenTexture, fragTexCoord).rgb * weight[0]; // Current fragment's contribution

    if (horizontal)
    {
        for (int i = 1; i < 11; ++i)
        {
            vec2 offset = vec2(tex_offset.x * float(i), 0.0);
            result += texture(screenTexture, fragTexCoord + offset).rgb * weight[i];
            result += texture(screenTexture, fragTexCoord - offset).rgb * weight[i];
        }
    }
    else
    {
        for (int i = 1; i < 11; ++i)
        {
            vec2 offset = vec2(0.0, tex_offset.y * float(i));
            result += texture(screenTexture, fragTexCoord + offset).rgb * weight[i];
            result += texture(screenTexture, fragTexCoord - offset).rgb * weight[i];
        }
    }

    FragColor = vec4(result, 1.0);
}
