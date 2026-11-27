#version 460
#include "common.glsl"

layout(push_constant) uniform PassPush {
    uvec4 ids;
    vec4 f0;
    vec4 f1;
    vec4 f2;
    mat4 m;
} pass;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

// bilinear depth: the 2x2 texels at floor(p) (clamped at the edges) in one gather, taken at the centre of that 2x2 block so the hardware
// picks exactly the texels the weights are for (gather order: x (0,1), y (1,1), z (1,0), w (0,0))
float DepthLinear(vec2 uv, ivec2 size) {
    vec2 p = clamp(uv, 0.0, 1.0) * vec2(size) - 0.5;
    vec2 a = floor(p);
    vec2 f = p - a;
    vec4 d = textureGather(sampler2D(images[IMG_DEPTH], samplers[SMP_POINT_CLAMP]), (a + 1.0) / vec2(size));
    return mix(mix(d.w, d.z, f.x), mix(d.x, d.y, f.x), f.y);
}

void main() {
    uint source = pass.ids.x;
    ivec2 size = ivec2(ImgSize(IMG_DEPTH));
    vec2 texel = 1.0 / vec2(size);
    vec2 uv = clamp((floor(gl_FragCoord.xy) + 1.0) * texel, 0.0, 1.0);
    vec4 center = Img(source, SMP_LINEAR_CLAMP, uv);
    if (DepthLinear(uv, size) > 1.0e-6) {
        out_color = center;
        return;
    }
    vec3 c = center.rgb;
    float k = 1.0;
    for (int level = 0; level < 8; ++level) {
        vec2 o = texel * k;
        k *= 1.5;
        vec2 taps[4] = vec2[](clamp(uv - vec2(o.x, 0.0), 0.0, 1.0), clamp(uv + vec2(o.x, 0.0), 0.0, 1.0), clamp(uv - vec2(0.0, o.y), 0.0, 1.0),
                              clamp(uv + vec2(0.0, o.y), 0.0, 1.0));
        bool covered = false;
        for (int i = 0; i < 4; ++i) {
            covered = covered || DepthLinear(taps[i], size) > 1.0e-6;
        }
        if (covered) {
            continue;
        }
        vec3 sum = c;
        for (int i = 0; i < 4; ++i) {
            sum += Img(source, SMP_LINEAR_CLAMP, taps[i]).rgb;
        }
        c = sum * 0.2;
    }
    out_color = vec4(Saturate(c), 1.0);
}
