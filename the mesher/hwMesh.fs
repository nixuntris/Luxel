#version 460

flat in vec3 color;
out vec4 FragColor;

uniform mat4 MVP;
uniform int offsetInt;
flat in float depthr;


layout(std430, binding = 0) buffer GBufferFrame {
    uint frame[];
};
layout(std430, binding = 1) buffer GBufferPos {
    vec3 pos[];
};
layout(std430, binding = 4) buffer GBufferDepth {
    int depth[];
};

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
layout(std430, binding = 2) buffer VerticesBuffer {
    uint vertices[];
};
uniform uint packedColors[1024];
uniform int voxelType;

uniform int frameIndex;
uniform int vHeight;
uniform int vWidth;

void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    
    
    if ((y + frameIndex) % 2 == 0) discard;

    int idx = (vHeight - y) * vWidth + x;
    int dInt = int(depthr);
    uint actualOld = atomicMin(depth[idx], dInt);
    if (actualOld > uint(dInt)) {
        frame[idx] = packRGBA8(vec4(color, 1.0));
        depth[idx] = int(floor(depthr));
    }
    else {
        discard;
    }
}