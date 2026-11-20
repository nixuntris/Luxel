#version 460
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_shader_ballot : require
#extension GL_ARB_shader_group_vote : require
layout (local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

const int chunkSize = 32;

layout(std430, binding = 0) buffer GBufferEx {
    uint frame[3840 * 2160];
    vec3 pos[3840*2160];
    int depth[3840* 2160];
    int voxelId[3840*2160];
    float lightVal[3840*2160];
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
    int regionLod = handles[index].lod;
    
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
   return 0; //the problem is here
    
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
    if (regionLod==32) return handles[index].filled; //the problem is here
    
    uvec2 handle = handles[index].handle;
    if (handle != uvec2(0)) {
        uimage3D fluidMap = uimage3D(handle);
        ivec3 lp = (pos & (chunkSize - 1)) / regionLod;
        uint r = 0u;
        bool pending = true;
        while (anyInvocationARB(pending)) {
            if (pending) {
                uvec2 cur = uvec2(readFirstInvocationARB(handle.x), readFirstInvocationARB(handle.y));
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
void main() {
    ivec3 chunkPosition = ivec3(gl_WorkGroupID.xyz);

    int index = chunkPosition.y * (wx * wz) + chunkPosition.z * wx + chunkPosition.x; //this is fine

    if (handles[index].lod!=1) return;
    for (int x = 0; x < 4; x++) {
        for (int z = 0; z <4; z++) {
            for (int y = 0; y < 4; y++) {
                
                ivec3 voxel = ivec3(gl_LocalInvocationID.xyz)*4+ivec3(x,y,z);
                ivec3 globalPosition = voxel+chunkPosition*chunkSize;
                int lodr = 0;
                uint id = readVoxelData(globalPosition,lodr);
                if (id==10) {
                    if (readVoxelData(globalPosition-ivec3(0,1,0),lodr)==0) {
                        setNibbleData(globalPosition,0);
                        setNibbleData(globalPosition-ivec3(0,1,0),10);
                    }
                }
            }
        }
    }

}
