#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
//in vec4 fragColor;
in vec3 fragNormal;

out vec4 finalColor;


void main()
{
    finalColor = vec4(1.0, 0.0, 0.0, 1.0);
}