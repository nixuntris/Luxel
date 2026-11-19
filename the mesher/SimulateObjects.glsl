#version 460
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_shader_ballot : require
#extension GL_ARB_shader_group_vote : require
layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

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
        if (nibble!=0) return 7;
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

void main() {
    ivec3 position = ivec3(gl_GlobalInvocationID.xyz);

    //given i have the camera position, i should be able to statically dispatch a certain work size and just offset it
    //distance under 960 gets lod 1
    //so i need to calculate it like that
}
