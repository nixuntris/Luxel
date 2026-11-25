#version 460
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_shader_ballot : require
#extension GL_ARB_shader_group_vote : require
const int groupSizeX = 8;
const int groupSizeY = 8;
const int groupSizeZ = 8;
layout (local_size_x = (32/groupSizeX), local_size_y = (32/groupSizeY), local_size_z = (32/groupSizeZ)) in;

const int chunkSize = 32;
uniform int waterResolution;
int cellsPerChunk = chunkSize / waterResolution;

layout(std430, binding = 0) buffer GBufferFrame {
    uint frame[];
};
layout(std430, binding = 1) buffer GBufferPos {
    vec3 pos[];
};
layout(std430, binding = 4) buffer GBufferDepth {
    int depth[];
};

layout(std430, binding = 11) buffer FluidSimulationData {
    int containsFluids[];
};

struct Region {
    uvec2 handle;
    uvec2 probeHandle;
    uvec2 fluidHandle;
    int lod;
    int filled;
};

layout(std430, binding = 3) buffer BitHandles {
    Region handles[];
};
layout(std430, binding = 10) buffer GPUBatch {
    int gpuBatch[1024*1024];
};

uniform vec3 camForward;
uniform int wx;
uniform int wy;
uniform int wz;
uniform int renderWidth;
uniform int renderHeight;
uniform vec3 camPos;
uniform mat4 matView;
uniform mat4 matProj;
uniform int frameIndex;
uniform vec3 cameraOffset;
uniform int workGroupIDOffset;

bool cellInBounds(ivec3 cellPos) {
    return !(any(lessThan(cellPos, ivec3(0))) ||
             cellPos.x >= wx * cellsPerChunk ||
             cellPos.y >= wy * cellsPerChunk ||
             cellPos.z >= wz * cellsPerChunk);
}

int chunkIndexOfCell(ivec3 cellPos) {
    ivec3 chunk = cellPos / cellsPerChunk;
    return chunk.y * (wx * wz) + chunk.z * wx + chunk.x;
}

uint readWaterData(ivec3 cellPos) {
    if (!cellInBounds(cellPos))
        return 0u;

    int index = chunkIndexOfCell(cellPos);
    uvec2 fluidhandle = handles[index].fluidHandle;
    if (fluidhandle == uvec2(0))
        return 0u;

    ivec3 lc = cellPos & (cellsPerChunk - 1);
    int nibbleIndex = lc.x & 1;
    ivec3 texel = ivec3(lc.x >> 1, lc.y, lc.z);

    uint r = 0u;
    bool pending = true;
    while (anyInvocationARB(pending)) {
        if (pending) {
            uvec2 cur = uvec2(
                readFirstInvocationARB(fluidhandle.x),
                readFirstInvocationARB(fluidhandle.y)
            );
            if (fluidhandle == cur) {
                r = imageLoad(uimage3D(cur), texel).x;
                pending = false;
            }
        }
    }
    return (r >> (uint(nibbleIndex) * 4u)) & 0x0Fu;
}

uint readSolidData(ivec3 pos) {
    if (any(lessThan(pos, ivec3(0))) ||
        pos.x >= wx * chunkSize ||
        pos.y >= wy * chunkSize ||
        pos.z >= wz * chunkSize)
        return 0u;

    ivec3 chunk = pos / chunkSize;
    int index = chunk.y * (wx * wz) + chunk.z * wx + chunk.x;
    int regionLod = handles[index].lod;

    if (regionLod == 32)
        return uint(handles[index].filled);
    if (regionLod == 0 || regionLod == 3000)
        return 0u;

    uvec2 handle = handles[index].handle;
    if (handle == uvec2(0))
        return 0u;

    ivec3 lp = (pos & (chunkSize - 1)) / regionLod;
    uint r = 0u;
    bool pending = true;
    while (anyInvocationARB(pending)) {
        if (pending) {
            uvec2 cur = uvec2(
                readFirstInvocationARB(handle.x),
                readFirstInvocationARB(handle.y)
            );
            if (handle == cur) {
                r = imageLoad(uimage3D(cur), lp).x;
                pending = false;
            }
        }
    }
    return r;
}

uint readCellSolid(ivec3 cellPos) {
    if (!cellInBounds(cellPos))
        return 1u;
    return readSolidData(cellPos * waterResolution);
}

void setNibbleData(ivec3 cellPos, uint value) {
    if (!cellInBounds(cellPos))
        return;

    int index = chunkIndexOfCell(cellPos);

    containsFluids[index] = 1;
    containsFluids[index+wx*wy*wz] = 1;
    int regionLod = handles[index].lod;

    if (regionLod == 32 || regionLod == 3000)
        return;

    uvec2 handle = handles[index].fluidHandle;
    if (handle == uvec2(0))
        return;

    ivec3 lc = cellPos & (cellsPerChunk - 1);
    int nibbleIndex = lc.x & 1;
    ivec3 texel = ivec3(lc.x >> 1, lc.y, lc.z);

    value &= 0x0Fu;

    bool pending = true;
    while (anyInvocationARB(pending)) {
        if (pending) {
            uvec2 cur = uvec2(
                readFirstInvocationARB(handle.x),
                readFirstInvocationARB(handle.y)
            );

            if (handle == cur) {
                uimage3D fluidMap = uimage3D(cur);
                uint r = imageLoad(fluidMap, texel).x;
                uint shift = uint(nibbleIndex) * 4u;
                uint mask = 0x0Fu << shift;
                r = (r & ~mask) | (value << shift);
                imageStore(fluidMap, texel, uvec4(r));
                pending = false;
            }
        }
    }
}

const ivec3 fallOffsets[5] = ivec3[5](
    ivec3(0,-1,0),
    ivec3(-1,-1,0),
    ivec3(1,-1,0),
    ivec3(0,-1,-1),
    ivec3(0,-1,1)
);

const ivec3 flowDirections[4] = ivec3[4](
    ivec3(-1, 0, 0),
    ivec3(1, 0, 0),
    ivec3(0, 0, -1),
    ivec3(0, 0, 1)
);

bool UpdateCellFalling(ivec3 position) {
    for (int i = 0; i < 5; i++) {
        ivec3 target = position + fallOffsets[i];

        if (!cellInBounds(target))
            continue;

        uint bottomValue = readCellSolid(target);
        uint waterBelow = readWaterData(target);

        if (bottomValue == 0u && waterBelow == 0u) {
            setNibbleData(position, 0u);
            setNibbleData(target, 8u);
            return true;
        }
    }
    return false;
}

bool UpdateCellWater(ivec3 position) {
    uint current = readWaterData(position);

    if (current == 0u)
        return false;

    int previousDirection = int(current & 7u) - 1;

    for (int i = 0; i < 4; i++) {
        int direction = (previousDirection + 1 + i) % 4;
        if (previousDirection >= 0 &&
            direction == (previousDirection ^ 1))
            continue;

        ivec3 target = position + flowDirections[direction];

        if (!cellInBounds(target))
            continue;

        uint targetVoxel = readCellSolid(target);
        uint targetWater = readWaterData(target);

        if (targetVoxel != 0u || targetWater != 0u)
            continue;

        ivec3 belowTarget = target + ivec3(0, -1, 0);

        uint supportVoxel = readCellSolid(belowTarget);
        uint supportWater = readWaterData(belowTarget);
        if (supportVoxel == 0u && supportWater == 0u)
            continue;

        setNibbleData(position, 0u);
        setNibbleData(target, uint(9 + direction));

        return true;
    }

    return false;
}

void main() {
    ivec3 chunkPosition = ivec3(
        gpuBatch[gl_WorkGroupID.x * 3 + 0],
        gpuBatch[gl_WorkGroupID.x * 3 + 1],
        gpuBatch[gl_WorkGroupID.x * 3 + 2]
    );
    int index = chunkPosition.y * (wx * wz) + chunkPosition.z * wx + chunkPosition.x;

    if (handles[index].lod != 1 && handles[index].lod != 32) return;

    const ivec3 localSize = ivec3(32/groupSizeX, 32/groupSizeY, 32/groupSizeZ);
    const ivec3 cellsPerInvocation = max(ivec3(cellsPerChunk) / localSize, ivec3(1));

    for (int x = 0; x < cellsPerInvocation.x; x++) {
        for (int z = 0; z < cellsPerInvocation.z; z++) {
            for (int y = 0; y < cellsPerInvocation.y; y++) {
                ivec3 localCell = ivec3(gl_LocalInvocationID.xyz) * cellsPerInvocation + ivec3(x, y, z);
                ivec3 cellPosition = localCell + chunkPosition * cellsPerChunk;
                if (readWaterData(cellPosition) != 0u) {
                    containsFluids[index+wx*wy*wz] = 1; //means it has a cell
                    if (!UpdateCellFalling(cellPosition)) {
                        if (UpdateCellWater(cellPosition)) {
                            containsFluids[index] = 1;
                        }
                    }
                    else {
                        containsFluids[index] = 1;
                    }
                }
            }
        }
    }
}