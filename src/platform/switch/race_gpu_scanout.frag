#version 310 es
precision highp float;
precision highp int;
layout(std430, binding=0) readonly buffer PackedVram { uint words[]; };
#ifdef PRESENT_PERSPECTIVE
layout(std430, binding=3) readonly buffer DisplayOverrides { uint displayOverrides[]; };
#endif
// Host validates the physical display rectangle lies within 1024x512,
// crop within bitmap, and offset in signed 16-bit range.
uniform ivec4 displayRect; // physical x,y and logical bitmap width,height
uniform ivec2 cropOrigin;  // bitmap coordinates, not a VRAM origin
uniform ivec2 displayOffset;
layout(location=0) out vec4 color;

uvec3 expand5(uvec3 c) { return (c << 3u) | (c >> 2u); }
void main()
{
    // Offscreen viewport starts at (0,0); texture row zero is the top game row.
    ivec2 source = ivec2(gl_FragCoord.xy) + cropOrigin - displayOffset;
    color = vec4(0.0, 0.0, 0.0, 1.0);
    if (any(lessThan(source, ivec2(0))) || any(greaterThanEqual(source, displayRect.zw)))
        return;
    ivec2 physical = displayRect.xy + source;
    uint index = uint(physical.y) * 1024u + uint(physical.x);
    uint pixel = words[index >> 1u] >> ((index & 1u) * 16u);
#ifdef PRESENT_PERSPECTIVE
    uint overridePixel = displayOverrides[index];
    if (overridePixel != 0u) pixel = overridePixel;
#endif
    // Match the byte order of the stock uint32 bitmap upload. The existing
    // present shader samples .bgr and supplies the final Y orientation.
    uvec3 bgr = uvec3(pixel >> 10u, pixel >> 5u, pixel) & 31u;
    color = vec4(vec3(expand5(bgr)) / 255.0, 1.0);
}
