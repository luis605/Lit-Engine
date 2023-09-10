#version 330 core
out vec4 FragColor;

in vec2 fragTexCoord;

uniform sampler2D scene;
uniform sampler2D bloomBlur;

void main()
{             
    vec3 hdrColor = texture(scene, fragTexCoord).rgb;      
    vec3 bloomColor = texture(bloomBlur, fragTexCoord).rgb;

    hdrColor = mix(hdrColor, bloomColor, 0.25);

    FragColor = vec4(hdrColor, 1.0);
}
