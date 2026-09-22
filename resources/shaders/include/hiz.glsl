uniform sampler2D u_hizTexture;

vec2 projectedExtent(float center, float depth, float radius) {
    float tangentLength = sqrt(center * center + depth * depth - radius * radius);
    float lo = (tangentLength * center - radius * depth) / (tangentLength * depth + radius * center);
    float hi = (tangentLength * center + radius * depth) / (tangentLength * depth - radius * center);
    return vec2(lo, hi);
}

bool sphereOccluded(vec3 worldCenter, float radius) {
    vec3 viewCenter = (view * vec4(worldCenter, 1.0)).xyz;
    float depth = -viewCenter.z;
    if (depth - radius < 0.1) return false;

    vec2 ndcX = projectedExtent(viewCenter.x, depth, radius) * projection[0][0];
    vec2 ndcY = projectedExtent(viewCenter.y, depth, radius) * projection[1][1];
    vec2 uvMin = clamp(vec2(min(ndcX.x, ndcX.y), -max(ndcY.x, ndcY.y)) * 0.5 + 0.5, 0.0, 1.0);
    vec2 uvMax = clamp(vec2(max(ndcX.x, ndcX.y), -min(ndcY.x, ndcY.y)) * 0.5 + 0.5, 0.0, 1.0);

    ivec2 baseSize = textureSize(u_hizTexture, 0);
    vec2 pixelMin = uvMin * vec2(baseSize);
    vec2 pixelMax = uvMax * vec2(baseSize);
    vec2 footprint = pixelMax - pixelMin;
    int mip = clamp(int(ceil(log2(max(max(footprint.x, footprint.y), 1.0)))), 0, textureQueryLevels(u_hizTexture) - 1);

    ivec2 lastTexel = textureSize(u_hizTexture, mip) - 1;
    ivec2 lo = min(ivec2(pixelMin) >> mip, lastTexel);
    ivec2 hi = min(ivec2(pixelMax) >> mip, lastTexel);
    float farthest = max(max(texelFetch(u_hizTexture, lo, mip).r, texelFetch(u_hizTexture, ivec2(hi.x, lo.y), mip).r),
                         max(texelFetch(u_hizTexture, ivec2(lo.x, hi.y), mip).r, texelFetch(u_hizTexture, hi, mip).r));

    vec4 nearestClip = projection * vec4(viewCenter.xy, viewCenter.z + radius, 1.0);
    return nearestClip.z / nearestClip.w > farthest;
}
