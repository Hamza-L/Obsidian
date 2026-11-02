#include <metal_stdlib>
using namespace metal;

struct ImageRW { texture2d<float, access::write> t; };   // one imageTable entry, write view

struct FillParams {
    uint  target;   // OImage.id of rt
    float time;
};

kernel void fill(uint2 id                  [[thread_position_in_grid]],
                 constant ImageRW*    images [[buffer(0)]],
                 constant FillParams& p      [[buffer(1)]])
{
    texture2d<float, access::write> out = images[p.target].t;
    if (id.x >= out.get_width() || id.y >= out.get_height()) return;

    float2 uv = float2(id) / float2(out.get_width(), out.get_height());
    out.write(float4(uv, 0.5 + 0.5 * sin(p.time), 1), id);
}
