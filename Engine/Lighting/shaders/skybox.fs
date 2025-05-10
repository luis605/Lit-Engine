/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#version 460 core

layout(location = 0) in highp vec3 fragPosition;
layout(location = 1) in vec2 fragTexCoord;

uniform samplerCube environmentMap;
uniform bool vflipped;
uniform bool doGamma;
uniform vec4 skyboxColor = vec4(1.0);

layout(std430, binding = 1) buffer ExposureBuffer {
    float exposure;
};

const int MAX_OBJECTS = 1;

struct Object {
    bool enabled;
    vec2 scale;
    vec2 rotation;
};

uniform sampler2D objectTextures[MAX_OBJECTS];
uniform Object objects[MAX_OBJECTS];
uniform int objectsCount;

out vec4 finalColor;

vec3 sphericalToCartesian(float pitch, float yaw) {
    float x = cos(pitch) * cos(yaw);
    float y = sin(pitch);
    float z = cos(pitch) * sin(yaw);
    return vec3(x, y, z);
}

vec2 calculateCubeUV(vec3 fragPos) {
    vec3 absFragPos = abs(fragPos);
    vec2 uv;

    if (absFragPos.x >= absFragPos.y && absFragPos.x >= absFragPos.z) {
        uv = fragPos.zx / absFragPos.x * 0.5 + 0.5;
    } else if (absFragPos.y >= absFragPos.x && absFragPos.y >= absFragPos.z) {
        uv = fragPos.xy / absFragPos.y * 0.5 + 0.5;
    } else {
        uv = fragPos.xy / absFragPos.z * 0.5 + 0.5;
    }

    return uv;
}

void main() {
    highp vec3 color = vec3(0.0);

    if (vflipped)
        color = texture(environmentMap, vec3(fragPosition.x, -fragPosition.y, fragPosition.z)).rgb;
    else
        color = texture(environmentMap, fragPosition).rgb;

    for (int i = 0; i < objectsCount; i++) {
        Object obj = objects[i];
        if (obj.enabled) {
            float pitch = radians(obj.rotation.x);
            float yaw = radians(obj.rotation.y);

            vec3 objectDir = sphericalToCartesian(pitch, yaw);
            float dotProduct = dot(normalize(fragPosition), objectDir);

            if (dotProduct > 0.95) {
                // Calculate UV for the cube face
                vec2 uv = calculateCubeUV(normalize(fragPosition - objectDir));

                float aspectRatio = textureSize(objectTextures[i], 0).x / textureSize(objectTextures[i], 0).y;
                uv.x *= aspectRatio;
                uv *= obj.scale;
                uv = clamp(uv, 0.0, 1.0);

                vec4 objectColor = texture(objectTextures[i], uv);
                color = mix(color, objectColor.rgb, objectColor.a);
            }
        }
    }

    color *= skyboxColor.rgb * exposure;

    if (doGamma) {
        color = pow(color, vec3(1.0 / 2.2));
    }

    finalColor = vec4(color, 1.0);
}
