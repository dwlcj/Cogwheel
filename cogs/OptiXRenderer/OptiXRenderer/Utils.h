// OptiX shading utilities.
// ---------------------------------------------------------------------------
// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#ifndef _OPTIXRENDERER_SHADING_UTILS_H_
#define _OPTIXRENDERER_SHADING_UTILS_H_

#include <OptiXRenderer/Defines.h>

#include <optixu/optixu_math_namespace.h>
#include <optixu/optixu_matrix_namespace.h>

namespace OptiXRenderer {

__inline_dev__ optix::float3 project_ray_direction(optix::float2 viewport_pos, 
                                                   const optix::float3& camera_position, 
                                                   const optix::Matrix4x4& inverted_view_projection_matrix) {
    using namespace optix;

    // Generate rays.
    float4 normalized_projected_pos = make_float4(viewport_pos.x * 2.0f - 1.0f,
                                                  viewport_pos.y * 2.0f - 1.0f,
                                                  -1.0f, 1.0f);

    float4 projected_world_pos = inverted_view_projection_matrix * normalized_projected_pos;

    float3 ray_origin = make_float3(projected_world_pos) / projected_world_pos.w;

    return normalize(ray_origin - camera_position);
}

// Computes a tangent and bitangent that together with the normal creates an orthonormal bases.
// Consider using TBN to wrap the tangents.
__inline_all__ static void compute_tangents(const optix::float3& normal,
                                            optix::float3& tangent, optix::float3& bitangent) {
    using namespace optix;

    float3 a0;
    if (abs(normal.x) < abs(normal.y)) {
        const float zup = abs(normal.z) < abs(normal.x) ? 0.0f : 1.0f;
        a0 = make_float3(zup, 0.0f, 1.0f - zup);
    } else {
        const float zup = (abs(normal.z) < abs(normal.y)) ? 0.0f : 1.0f;
        a0 = make_float3(0.0f, zup, 1.0f - zup);
    }

    bitangent = normalize(cross(normal, a0));
    tangent = normalize(cross(bitangent, normal));
}

__inline_all__ optix::float3 clamp_light_contribution_by_path_PDF(const optix::float3& radiance, float path_PDF, int accumulations) {
#if PATH_PDF_FIREFLY_FILTER
    float contribution = optix::luminance(radiance);
    float max_contribution = (1.0f / (1.0f - path_PDF)) - 1.0f;
    max_contribution *= (accumulations + 1);
    max_contribution += 1.0f;
    return radiance * fminf(max_contribution / contribution, 1.0f);
#else
    return radiance;
#endif
}

//-----------------------------------------------------------------------------
// Math utils
//-----------------------------------------------------------------------------

__inline_all__ float average(const optix::float3& v) {
    return (v.x + v.y + v.z) / 3.0f;
}

__inline_all__ float heaviside(float v) {
    return v >= 0.0f ? 1.0f : 0.0f;
}

__inline_all__ float sign(float v) {
    return v >= 0.0f ? 1.0f : -1.0f;
}

__inline_all__ float sum(const optix::float3& v) {
    return v.x + v.y + v.z;
}

__inline_all__ optix::float3 gammacorrect(const optix::float3& color, float gamma) {
    return optix::make_float3(pow(color.x, gamma),
                              pow(color.y, gamma),
                              pow(color.z, gamma));
}

__inline_all__ optix::float4 gammacorrect(const optix::float4& color, float gamma) {
    return optix::make_float4(pow(color.x, gamma),
                              pow(color.y, gamma),
                              pow(color.z, gamma),
                              color.w);
}

__inline_all__ bool is_PDF_valid(float PDF) {
    return PDF > 0.000001f;
}

__inline_all__ static float pow5(float x) {
    float xx = x * x;
    return xx * xx * x;
}

__inline_all__ optix::float2 direction_to_latlong_texcoord(const optix::float3& direction) {
    float u = (atan2f(direction.x, direction.z) + PIf) * 0.5f / PIf;
    float v = (asinf(direction.y) + M_PIf * 0.5f) / PIf;
    return optix::make_float2(u, v);
}

__inline_all__ optix::float3 latlong_texcoord_to_direction(const optix::float2& uv) {
    float phi = uv.x * 2.0f * PIf;
    float theta = uv.y * PIf;
    float sin_theta = sinf(theta);
    return optix::make_float3(sin_theta * sinf(phi), cosf(theta), sin_theta * cosf(phi));
}

} // NS OptiXRenderer

#endif // _OPTIXRENDERER_SHADING_UTILS_H_