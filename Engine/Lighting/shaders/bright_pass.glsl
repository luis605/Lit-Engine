#version 330 core

in vec2 fragTexCoord;
out vec4 fragColor;

uniform sampler2D colorTexture;

void main()
{
    // Sample the input texture
    vec4 texColor = texture(colorTexture, fragTexCoord);
    float brightnessThreshold = 0.15f;
	float brightness = dot(texColor.rgb, vec3(0.2126f, 0.7152f, 0.0722f));
    if(brightness > 0.15f)
        fragColor = vec4(texColor.rgb, 1.0f);
    else
        fragColor = texColor * (brightnessThreshold / brightness);
}
