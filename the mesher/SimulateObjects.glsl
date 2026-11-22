#version 460
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_shader_ballot : require
#extension GL_ARB_shader_group_vote : require
const int chunkSize = 32;

const int groupSizeX = 8;
const int groupSizeY = 8;
const int groupSizeZ = 8;
layout (local_size_x = groupSizeX, local_size_y = groupSizeY, local_size_z = groupSizeZ) in;

layout(std430, binding = 0) buffer GBufferEx {
    uint frame[3840 * 2160];
    vec3 pos[3840*2160];
    int depth[3840* 2160];
    int voxelId[3840*2160];
    float lightVal[3840*2160];
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
uint readWaterData(ivec3 pos) {
    if (any(lessThan(pos, ivec3(0))) ||
        pos.x >= wx * chunkSize ||
        pos.y >= wy * chunkSize ||
        pos.z >= wz * chunkSize)
        return 0u;

    ivec3 chunk = pos / chunkSize;
    int index = chunk.y * (wx * wz) + chunk.z * wx + chunk.x;

    uvec2 handle = handles[index].fluidHandle;

    if (handle == uvec2(0))
        return 0u;

    ivec3 lp = pos & (chunkSize - 1);
    int nibbleIndex = lp.x & 1;
    lp.x /= 2;

    uint r = imageLoad(uimage3D(handle), lp).x;

    return (r >> (uint(nibbleIndex) * 4u)) & 0x0Fu;
}
uint readVoxelData(ivec3 pos, inout int lod) {
    if (any(lessThan(pos, ivec3(0))) ||
        pos.x >= wx * chunkSize ||
        pos.y >= wy * chunkSize ||
        pos.z >= wz * chunkSize)
        return 0u;   
    ivec3 chunk = pos / chunkSize;
    int index = chunk.y * (wx * wz) + chunk.z * wx + chunk.x;
    int regionLod = handles[index].lod;
    lod = regionLod;

    uvec2 fluidhandle = handles[index].fluidHandle;
    if (fluidhandle!=uvec2(0)) {

        uimage3D fluidMap = uimage3D(fluidhandle);
        ivec3 lp = (pos & (chunkSize - 1));
        int nibbleIndex = lp.x & 1;  
        lp.x /= 2;
        uint r = 0u;
        bool pending = true;
        while (anyInvocationARB(pending)) {
            if (pending) {
                uvec2 cur = uvec2(readFirstInvocationARB(fluidhandle.x), readFirstInvocationARB(fluidhandle.y));
                if (fluidhandle == cur) {
                    r = imageLoad(uimage3D(cur), lp).x;
                    pending = false;
                }
            }
        }
        uint byteIndex  = lp.x;        
        uint shift = nibbleIndex*4;
        uint nibble  = (r >> shift) & 0x0F;
        if (nibble!=0) return 10;
    }
    if (regionLod==32) return handles[index].filled; 

    uvec2 handle = handles[index].handle;
    if (handle != uvec2(0)) {
        uimage3D fluidMap = uimage3D(handle);
        ivec3 lp = (pos & (chunkSize - 1)) / regionLod;
        uint r = 0u;
        bool pending = true;
        uvec2 cur = handle;

        while (anyInvocationARB(pending)) {
            if (pending) {
                cur = uvec2(
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
    return 0u;
}

void setVoxelData(ivec3 pos, uint value) {
    if (any(lessThan(pos, ivec3(0))) ||
        pos.x >= wx * chunkSize ||
        pos.y >= wy * chunkSize ||
        pos.z >= wz * chunkSize)
        return;

    ivec3 chunk = pos / chunkSize;
    int index = chunk.y * (wx * wz) + chunk.z * wx + chunk.x;

    int regionLod = handles[index].lod;

    if (regionLod == 32 || regionLod == 3000)
        return;

    uvec2 handle = handles[index].handle;

    if (handle != uvec2(0)) {
        ivec3 lp = (pos & (chunkSize - 1)) / regionLod;

        bool pending = true;

        while (anyInvocationARB(pending)) {
            if (pending) {
                uvec2 cur = uvec2(
                    readFirstInvocationARB(handle.x),
                    readFirstInvocationARB(handle.y)
                );

                if (handle == cur) {
                    imageStore(uimage3D(cur), lp, uvec4(value));
                    pending = false;
                }
            }
        }
    }
}
void setNibbleData(ivec3 pos, uint value) {
    if (any(lessThan(pos, ivec3(0))) ||
        pos.x >= wx * chunkSize ||
        pos.y >= wy * chunkSize ||
        pos.z >= wz * chunkSize)
        return;

    ivec3 chunk = pos / chunkSize;
    int index = chunk.y * (wx * wz) + chunk.z * wx + chunk.x;

    containsFluids[index] = 1;
    int regionLod = handles[index].lod;

    if (regionLod == 32 || regionLod == 3000)
        return;

    uvec2 handle = handles[index].fluidHandle;
    if (handle == uvec2(0))
        return;

    ivec3 lp = pos & (chunkSize - 1);
    int nibbleIndex = lp.x & 1;      // 0 = low nibble, 1 = high nibble
    lp.x /= 2;

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

                uint r = imageLoad(fluidMap, lp).x;

                uint shift = uint(nibbleIndex) * 4u;
                uint mask  = 0x0Fu << shift;
                r = (r & ~mask) | (value << shift);

                imageStore(fluidMap, lp, uvec4(r));
                pending = false;
            }
        }
    }
}
uniform vec3 cameraOffset;
const ivec3 checkOffsets[5] = ivec3[5](
    ivec3(0,-1,0),
    ivec3(-1,-1,0),
    ivec3(1,-1,0),
    ivec3(0,-1,-1),
    ivec3(0,-1,1)
);
bool UpdateCellFalling(ivec3 position) {
    const ivec3 offsets[5] = ivec3[5](
        ivec3(0,-1,0),
        ivec3(-1,-1,0),
        ivec3(1,-1,0),
        ivec3(0,-1,-1),
        ivec3(0,-1,1)
    );

    int lodr = 0;

    for (int i = 0; i < 5; i++) {
        ivec3 target = position + offsets[i];

        uint bottomValue = readVoxelData(target, lodr);
        uint waterBelow = readWaterData(target);

        if (bottomValue == 0 && waterBelow == 0) {
            setNibbleData(position, 0);
            setNibbleData(target, 8);
            return true;
        }
    }

    return false;
}
bool UpdateCellWater(ivec3 position) {
    const ivec3 directions[4] = ivec3[4](
        ivec3(-1, 0, 0),
        ivec3(1, 0, 0),
        ivec3(0, 0, -1),
        ivec3(0, 0, 1)
    );

    uint current = readWaterData(position);

    if (current == 0)
        return false;

    int previousDirection = int(current & 7u) - 1;

    for (int i = 0; i < 4; i++) {
        int direction = (previousDirection + 1 + i) % 4;
        if (previousDirection >= 0 &&
            direction == (previousDirection ^ 1))
            continue;

        ivec3 target = position + directions[direction];

        int lodr = 0;

        uint targetVoxel = readVoxelData(target, lodr);
        uint targetWater = readWaterData(target);

        if (targetVoxel != 0 || targetWater != 0)
            continue;

        ivec3 belowTarget = target + ivec3(0, -1, 0);

        uint supportVoxel = readVoxelData(belowTarget, lodr);
        uint supportWater = readWaterData(belowTarget);
        if (supportVoxel == 0 && supportWater == 0)
            continue;
        setNibbleData(position, 0);
        setNibbleData(target, uint(9 + direction));

        return true;
    }

    return false;
}

uniform int workGroupIDOffset;
uniform int workGroupSpread;
void main() {
    ivec3 workGroup = ivec3(gl_WorkGroupID.xyz);

    workGroup.x = workGroup.x * workGroupSpread + workGroupIDOffset;

    if (workGroup.x >= wx)
        return;
    ivec3 chunkPosition = workGroup + ivec3(cameraOffset);
    int index = chunkPosition.y * (wx * wz) + chunkPosition.z * wx + chunkPosition.x; 

    if (handles[index].lod!=1 && handles[index].lod!=32) return;
    for (int x = 0; x < 32/groupSizeX; x++) {
        for (int z = 0; z < 32/groupSizeZ; z++) {
            for (int y = 0; y < 32/groupSizeY; y++) {

                ivec3 voxel = ivec3(gl_LocalInvocationID.xyz)*ivec3(32/groupSizeX,32/groupSizeY,32/groupSizeZ)+ivec3(x,y,z);
                ivec3 globalPosition = voxel+chunkPosition*chunkSize;
                int lodr;
                uint id = readWaterData(globalPosition);
                if (id != 0) {
                    containsFluids[index] = 1;
                    if (!UpdateCellFalling(globalPosition)) {
                        UpdateCellWater(globalPosition);
                    }
                }
            }
        }
    }

}