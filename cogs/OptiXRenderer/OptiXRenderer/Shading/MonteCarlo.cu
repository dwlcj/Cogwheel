// OptiX path tracing ray generation and miss program.
// ---------------------------------------------------------------------------
// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#include <OptiXRenderer/Types.h>

#include <optix.h>

using namespace OptiXRenderer;
using namespace optix;

// Ray params
rtDeclareVariable(Ray, ray, rtCurrentRay, );
rtDeclareVariable(MonteCarloPRD, monte_carlo_PRD, rtPayload, );

// Material params
rtDeclareVariable(float3, g_color, , );

//----------------------------------------------------------------------------
// Closest hit program for monte carlo sampling rays.
//----------------------------------------------------------------------------

rtDeclareVariable(float3, geometric_normal, attribute geometric_normal, );
rtDeclareVariable(float3, shading_normal, attribute shading_normal, );
rtDeclareVariable(float2, texcoord, attribute texcoord, );

RT_PROGRAM void closest_hit() {
    // const float3 world_geometric_normal = normalize(rtTransformNormal(RT_OBJECT_TO_WORLD, geometric_normal));
    const float3 world_shading_normal = normalize(rtTransformNormal(RT_OBJECT_TO_WORLD, shading_normal));
    // const float3 color = make_float3(texcoord.x, texcoord.y, 1.0f - texcoord.x - texcoord.y);
    monte_carlo_PRD.color = g_color * abs(dot(world_shading_normal, ray.direction));
}

//----------------------------------------------------------------------------
// Miss program for monte carlo rays.
//----------------------------------------------------------------------------

RT_PROGRAM void miss() {
    monte_carlo_PRD.color = make_float3(0.68f, 0.92f, 1.0f);
}