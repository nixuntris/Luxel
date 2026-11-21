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

uint readWaterData(ivec3 pos) {
    if (any(lessThan(pos, ivec3(0))) ||
        pos.x >= wx * chunkSize ||
        pos.y >= wy * chunkSize ||
        pos.z >= wz * chunkSize)
        return 0u;

    ivec3 chunk = pos / chunkSize;
    int index = chunk.y * (wx * wz) + chunk.z * wx + chunk.x;

    uvec2 fluidhandle = handles[index].fluidHandle;
    if (fluidhandle == uvec2(0))
        return 0u;

    ivec3 lp = pos & (chunkSize - 1);
    int nibbleIndex = lp.x & 1;
    lp.x /= 2;

    uint r = 0u;
    bool pending = true;

    while (anyInvocationARB(pending)) {
        if (pending) {
            uvec2 cur = uvec2(
                readFirstInvocationARB(fluidhandle.x),
                readFirstInvocationARB(fluidhandle.y)
            );

            if (fluidhandle == cur) {
                r = imageLoad(uimage3D(cur), lp).x;
                pending = false;
            }
        }
    }

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

uniform uint packedColors[1024];
uint hashVec3(vec3 v) {
    uint x = uint(v.x);
    uint y = uint(v.y);
    uint z = uint(v.z);
    uint h = x * 374761393u + y * 668265263u + z * 1274126177u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
vec4 unpackRGBA8(uint packedr)
{
    return vec4(
        float( packedr        & 0xFFu),
        float((packedr >>  8) & 0xFFu),
        float((packedr >> 16) & 0xFFu),
        float((packedr >> 24) & 0xFFu)
    ) * (1.0 / 255.0);
}
uint packRGBA8(vec4 color)
{
   vec4 c = clamp(color, 0.0, 1.0);
   uvec4 bytes = uvec4(c * 255.0 + 0.5);

   return
        ( bytes.r        ) |
        ( bytes.g <<  8 ) |
        ( bytes.b << 16 ) |
        ( bytes.a << 24 );
}

void skipChunk(inout vec3 cell, inout vec3 tMax, ivec3 stepDir, vec3 tDelta)
{
    ivec3 chunkMin = (ivec3(cell) / chunkSize) * chunkSize;
    ivec3 chunkMax = chunkMin + chunkSize;
    ivec3 nI = mix(ivec3(cell) - chunkMin + 1,
                   chunkMax - ivec3(cell),
                   greaterThan(stepDir, ivec3(0)));
    vec3 n = vec3(nI);
    vec3 tAxisExit = tMax + (n - 1.0) * tDelta;
    float tExit = min(tAxisExit.x, min(tAxisExit.y, tAxisExit.z));
    vec3 k = floor((tExit - tMax) / tDelta) + 1.0;
    k = clamp(k, vec3(0.0), n);
    k = mix(k, n, equal(tAxisExit, vec3(tExit)));

    ivec3 ki = ivec3(k);
    cell += stepDir * ki;
    tMax += tDelta * vec3(ki);
}
const vec3 waterAbsorb = vec3(0.45,0.07,0.03);
const vec3 waterScatter = vec3(0.05, 0.35, 0.55);

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    pixel.y *= 2;
    pixel.x *= 2;
    pixel.y += (frameIndex + 1) % 2;
    if (pixel.x >= renderWidth || pixel.y >= renderHeight) return;

    int pixelIndex = pixel.y * 3840 + pixel.x;

    vec2 ndc = vec2((pixel.x + 0.5) / renderWidth * 2.0 - 1.0,
                    1.0 - (pixel.y + 0.5) / renderHeight * 2.0);
    vec4 farPoint = inverse(matProj * matView) * vec4(ndc, 1.0, 1.0);
    vec3 rayDir = normalize(farPoint.xyz / farPoint.w - camPos);
    rayDir = mix(rayDir, vec3(1e-6), lessThan(abs(rayDir), vec3(1e-6)));

    vec3 cell = ivec3(floor(camPos));
    ivec3 stepDir = ivec3(sign(rayDir));
    vec3 crossingStep = abs(1.0 / rayDir);
    vec3 nextCrossing = (vec3(cell) + vec3(max(stepDir, ivec3(0))) - camPos) / rayDir;

    float opaqueDistance = float(depth[pixelIndex]) / dot(rayDir, camForward);
    float firstWaterDistance = -1.0;
    vec3 surfaceNormal = vec3(0.0, 1.0, 0.0);
    float waterThickness = 0.0;
    ivec3 enteredWaterPosition = ivec3(-1);
    for (int i = 0; i < 1000; i++) {
        vec3 entryTimes = nextCrossing - crossingStep;
        float enterDistance = max(max(max(entryTimes.x, entryTimes.y), entryTimes.z), 0.0);
        if (enterDistance >= opaqueDistance) break;

        int regionLod;
        uint voxel = readVoxelData(ivec3(cell), regionLod);

        if (any(lessThan(cell, vec3(0))) ||
            cell.x >= float(wx * chunkSize) ||
            cell.y >= float(wy * chunkSize) ||
            cell.z >= float(wz * chunkSize) ||
            enterDistance > 1000.0)
            break;

        if (regionLod == 0) { skipChunk(cell, nextCrossing, stepDir, crossingStep); continue; }
        if (voxel != 0u && voxel != 10u) break;

        float exitDistance = min(nextCrossing.x, min(nextCrossing.y, nextCrossing.z));

        if (voxel == 10u) {
            if (enteredWaterPosition==ivec3(-1)) {
                enteredWaterPosition = ivec3(floor(cell));
            }
            if (firstWaterDistance < 0.0) {
                firstWaterDistance = enterDistance;
                if (enterDistance > 0.0) {
                    vec3 enteredAxis = vec3(equal(entryTimes, vec3(enterDistance)));
                    surfaceNormal = -normalize(enteredAxis * vec3(stepDir));
                }
            }
            waterThickness += max(min(exitDistance, opaqueDistance) - enterDistance, 0.0);
        }

        ivec3 axisToStep = ivec3(lessThanEqual(nextCrossing.xyz, min(nextCrossing.yzx, nextCrossing.zxy)));
        cell += stepDir * axisToStep;
        nextCrossing += crossingStep * vec3(axisToStep);
    }

    if (firstWaterDistance < 0.0) return;

    vec3 hitPosition = camPos + rayDir * firstWaterDistance;
    if (surfaceNormal.y > 0.5) {
        float rippleX = float(hashVec3(floor(hitPosition * 4.0) + float(frameIndex / 8))) / 4294967295.0;
        float rippleZ = float(hashVec3(floor(hitPosition * 4.0).zxy + float(frameIndex / 8))) / 4294967295.0;
        surfaceNormal = normalize(surfaceNormal + 0.08 * vec3(rippleX - 0.5, 0.0, rippleZ - 0.5));
    }

    float cosIncidence = clamp(dot(-rayDir, surfaceNormal), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - cosIncidence, 5.0);

    vec2 refractionOffset = surfaceNormal.xz * 6.0 * (1.0 - cosIncidence * 0.5);
    ivec2 refractedPixel = clamp(pixel + ivec2(refractionOffset), ivec2(0), ivec2(renderWidth - 1, renderHeight - 1));
    int refractedIndex = refractedPixel.y * 3840 + refractedPixel.x;
    bool refractedSampleIsValid = float(depth[refractedIndex]) > firstWaterDistance * dot(rayDir, camForward);

    vec3 background = unpackRGBA8(frame[refractedSampleIsValid ? refractedIndex : pixelIndex]).rgb;
    vec3 transmittance = exp(-waterAbsorb * waterThickness);
    vec3 skyColor = vec3(0.53, 0.75, 0.95);
    vec3 waterBody = background * transmittance + waterScatter * skyColor * (1.0 - transmittance);

    int waterDistance = int(dot(cell - camPos, camForward));
    vec3 reflectedColor = skyColor;
    if (fresnel > 0.03) {
        vec3 marchPos = hitPosition + surfaceNormal * 0.01;
        vec3 reflectedDir = reflect(rayDir, surfaceNormal);
        for (int i = 0; i < 100; i++) {
            marchPos += reflectedDir * 0.5;
            int lod;
            uint voxel = readVoxelData(ivec3(floor(marchPos)), lod);
            if (voxel != 0u && voxel != 10u) {
                reflectedColor = unpackRGBA8(packedColors[voxel]).rgb;
                break;
            }
        }
    }

    vec3 finalColor = mix(waterBody, reflectedColor, fresnel);
    frame[pixelIndex] = packRGBA8(vec4(finalColor, 1.0));
}
