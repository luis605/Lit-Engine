#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;

// Output fragment color
out vec4 finalColor;

// Input lighting values
uniform vec4 ambient;
uniform vec3 viewPos;

float rand(vec2 co){
    // Generate a random number between 0 and 1
    float r = fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);

    // Map the random number to a range of colors
    return r;
}

void main()
{
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);

    vec4 color = texture2D(texture0, fragTexCoord);

    finalColor = vec4(rand(vec2(gl_PrimitiveID + vec2(color.x/1.03 + color.y*1.5))), rand(vec2(gl_PrimitiveID  + vec2(color.x/1.3 + color.y*1.05))), rand(vec2(gl_PrimitiveID  + vec2(color.x*1.2 + color.y*1.5))), 1);

}