#ifndef LIT_INSTANCING_GLSL
#define LIT_INSTANCING_GLSL

// GPU instancing (docs/Instancing.md, Engine.Instancing). A visible entry whose id has INSTANCE_ENTRY_BIT refers to slot
// (id & INSTANCE_INDEX_MASK) of the instance pool; its world matrix is the batch entity's world matrix times the
// instance's compact local transform. Includers that use instanceWorld() define LIT_INSTANCE_OWNER_WORLD(entity)
// (a mat4 from their WorldMatrixBuffer) before including this file.

const uint INSTANCE_ENTRY_BIT = 0x10000000u;
const uint INSTANCE_INDEX_MASK = 0x0FFFFFFFu;
const uint INSTANCE_CELL_SHIFT = 6u;
const uint INSTANCE_CELL_SIZE = 64u;
const uint INSTANCE_DEAD = 1u;
const uint INSTANCE_NO_SHADOW = 2u;
const uint INSTANCE_BATCH_ACTIVE = 1u;
const uint INSTANCE_BATCH_VISIBLE = 2u;
const uint INSTANCE_BATCH_SHADOWS = 4u;

struct InstanceGpu {
    vec4 positionScale;  // batch-local position, uniform scale
    uvec4 packed;        // x quaternion xy (snorm16), y zw, z colour RGBA8, w seed (24) | flags << 24
};

struct InstanceCellGpu {
    vec4 sphere;  // batch-local bounds of the live instances (w < 0: none)
    uvec4 info;   // x batch, y instance count, z live count
};

struct InstanceBatchGpu {
    uvec4 info;        // x owner entity, y mesh slot, z packed renderable (renderable.glsl layout), w flags
    vec4 meshSphere;   // mesh-local bounds
    vec4 lod;          // x lod bias, y fade start, z draw distance (0: unlimited)
    uvec4 cells;       // x first cell, y cell count, z instance count
};

layout(std430) readonly buffer InstanceBuffer {
    InstanceGpu instances[];
};

layout(std430) readonly buffer InstanceCellBuffer {
    InstanceCellGpu instanceCells[];
};

layout(std430) readonly buffer InstanceBatchBuffer {
    InstanceBatchGpu instanceBatches[];
};

bool isInstanceEntry(uint id) { return (id & INSTANCE_ENTRY_BIT) != 0u; }
uint instanceSlot(uint id) { return id & INSTANCE_INDEX_MASK; }
uint instanceBatchIndex(uint slot) { return instanceCells[slot >> INSTANCE_CELL_SHIFT].info.x; }

vec4 instanceQuat(InstanceGpu i) { return normalize(vec4(unpackSnorm2x16(i.packed.x), unpackSnorm2x16(i.packed.y))); }

mat3 quatToMat3(vec4 q) {
    vec3 q2 = q.xyz * 2.0;
    float xx = q.x * q2.x, yy = q.y * q2.y, zz = q.z * q2.z;
    float xy = q.x * q2.y, xz = q.x * q2.z, yz = q.y * q2.z;
    float wx = q.w * q2.x, wy = q.w * q2.y, wz = q.w * q2.z;
    return mat3(vec3(1.0 - (yy + zz), xy + wz, xz - wy), vec3(xy - wz, 1.0 - (xx + zz), yz + wx), vec3(xz + wy, yz - wx, 1.0 - (xx + yy)));
}

mat4 instanceLocal(InstanceGpu i) {
    mat3 r = quatToMat3(instanceQuat(i)) * i.positionScale.w;
    return mat4(vec4(r[0], 0.0), vec4(r[1], 0.0), vec4(r[2], 0.0), vec4(i.positionScale.xyz, 1.0));
}

vec4 instanceColorOf(InstanceGpu i) { return unpackUnorm4x8(i.packed.z); }
uint instanceSeedOf(InstanceGpu i) { return i.packed.w & 0x00FFFFFFu; }
uint instanceFlagsOf(InstanceGpu i) { return i.packed.w >> 24u; }

// Same hash as Engine.Instancing instanceHash01.
float instanceHash01(uint seed) {
    uint h = seed * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return float(h & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

// Mesh-local sphere of the batch's mesh moved into batch space by the instance transform.
vec4 instanceLocalSphere(InstanceGpu i, vec4 meshSphere) {
    vec3 center = i.positionScale.xyz + quatToMat3(instanceQuat(i)) * (meshSphere.xyz * i.positionScale.w);
    return vec4(center, meshSphere.w * abs(i.positionScale.w));
}

float maxAxisScale(mat4 m) { return max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz))); }

#ifdef LIT_INSTANCE_OWNER_WORLD
mat4 instanceWorld(uint slot) {
    InstanceBatchGpu batch = instanceBatches[instanceBatchIndex(slot)];
    return LIT_INSTANCE_OWNER_WORLD(batch.info.x) * instanceLocal(instances[slot]);
}
#endif

#endif
