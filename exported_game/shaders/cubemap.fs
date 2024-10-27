#version 330

in vec3 fragPosition;
out vec4 finalColor;

uniform sampler2D equirectangularMap;

struct Object {
    bool enabled;
    vec2 scale;
    vec2 velocity;
    vec2 rotation;
    sampler2D objectTexture;
};

layout(std430, binding = 0) buffer ObjectsBuffer {
    Object objects[];
};

uniform int objectsCount;

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = SampleSphericalMap(normalize(fragPosition));

    vec3 color = texture(equirectangularMap, uv).rgb;

    for (int i = 0; i < objectsCount; i++) {
        if (objects[i].enabled) {
            vec2 objUV = uv;
            float rotationAngle = objects[i].rotation.x;
            mat2 rotationMatrix = mat2(cos(rotationAngle), -sin(rotationAngle), sin(rotationAngle), cos(rotationAngle));

            objUV = (rotationMatrix * (objUV - 0.5)) + 0.5;
            objUV += objects[i].velocity;
            objUV = (objUV - 0.5) * objects[i].scale + 0.5;
            vec4 objectColor = texture(objects[i].objectTexture, objUV);

            color = mix(color, objectColor.rgb, objectColor.a);
        }
    }

    finalColor = vec4(color, 1.0);
}
