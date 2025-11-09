#version 430 core

layout(location = 0) out float FragColor;

void main()
{
    FragColor = gl_FragCoord.z;
}