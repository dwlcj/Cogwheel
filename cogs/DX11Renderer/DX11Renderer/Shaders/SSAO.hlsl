// SSAO shaders.
// ------------------------------------------------------------------------------------------------
// Copyright (C) 2018, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License.
// See LICENSE.txt for more detail.
// ------------------------------------------------------------------------------------------------

#include "RNG.hlsl"
#include "Utils.hlsl"

// ------------------------------------------------------------------------------------------------
// Constants.
// ------------------------------------------------------------------------------------------------

cbuffer constants : register(b0) {
    float world_radius;
    float bias;
    float intensity_over_sample_count; // Precomputed as (2 * intensity_scale / sample_count)
    float falloff;
    float recip_double_normal_variance;
    float recip_double_plane_variance;
    int sample_count;
    int filter_support;
    float max_depth_filtered_mipmap;
    float __padding;
    float2 g_buffer_size;
    float2 recip_g_buffer_viewport_size;
    float2 g_buffer_max_uv;
    int2 g_buffer_to_ao_index_offset;
    float2 ao_buffer_size;
};

cbuffer uv_offset_constants : register(b1) {
    float4 packed_uv_offsets[128];
}

static float2 uv_offsets[256] = ((float2[256])packed_uv_offsets);

cbuffer scene_variables : register(b13) {
    SceneVariables scene_vars;
};

static const float depth_far_plane_sentinel = 65504.0f;

Texture2D normal_tex : register(t0);
Texture2D depth_tex : register(t1);

SamplerState trilinear_sampler : register(s1);

// ------------------------------------------------------------------------------------------------
// SSAO utility functions.
// ------------------------------------------------------------------------------------------------

// Transform linear depth to view-space position using a perspective projection matrix.
// https://mynameismjp.wordpress.com/2009/03/10/reconstructing-position-from-depth/
float3 perspective_position_from_linear_depth(float z, float2 viewport_uv, float4x4 inverted_projection_matrix) {
    // Get x/w and y/w from the viewport position
    float x_over_w = viewport_uv.x * 2 - 1;
    float y_over_w = 1 - 2 * viewport_uv.y;

    // Transform by the inverse projection matrix
    float2 projected_view_pos = { x_over_w * inverted_projection_matrix._m00,
                                  y_over_w * inverted_projection_matrix._m11 };
    // Divide by w to get the view-space position
    return float3(projected_view_pos.xy * z, z);
}

// Transform view position to uv in screen space.
float2 uv_from_view_position(float3 view_position) {
    float4 _projected_view_pos = mul(float4(view_position, 1), scene_vars.projection_matrix);
    float3 projected_view_pos = _projected_view_pos.xyz * rcp(_projected_view_pos.w);

    // Transform from normalized screen space to uv.
    return float2(projected_view_pos.x * 0.5 + 0.5, 1 - (projected_view_pos.y + 1) * 0.5);
}

// Transform view position to u(v) in screen space.
// Assumes that the projection_matrix is a perspective projection matrix with 0'es in most entries.
float u_coord_from_view_position(float3 view_position) {
    // float x = dot(float4(view_position, 1), scene_vars.projection_matrix._m00_m10_m20_m30);
    // float w = dot(float4(view_position, 1), scene_vars.projection_matrix._m03_m13_m23_m33);
    float x = view_position.x * scene_vars.projection_matrix._m00;
    float w = view_position.z * scene_vars.projection_matrix._m23;
    float projected_view_pos_x = x * rcp(w);

    // Transform from normalized screen space to uv.
    return projected_view_pos_x * 0.5 + 0.5;
}

float2 cosine_disk_sampling(float2 sample_uv) {
    float r = sample_uv.x;
    float theta = TWO_PI * sample_uv.y;
    return r * float2(cos(theta), sin(theta));
}

// Returns a position for the tap on a unit disk.
float2 tap_location(int sample_number, int sample_count, float spin_angle) {
    const float spiral_turns = 73856093;
    float alpha = float(sample_number + 0.5) / sample_count;
    float angle = alpha * (spiral_turns * TWO_PI) + spin_angle;
    return float2(cos(angle), sin(angle)) * alpha;
}

float2x2 generate_rotation_matrix(float angle) {
    float cos_angle = cos(angle);
    float sin_angle = sin(angle);
    float2x2 mat = { cos_angle, -sin_angle,   // row 1
                     sin_angle,  cos_angle }; // row 2
    return mat;
}

// ------------------------------------------------------------------------------------------------
// Vertex shader.
// ------------------------------------------------------------------------------------------------

struct Varyings {
    float4 position : SV_POSITION;
    float4 uvs : TEXCOORD; // [ao-uv, projection-uv]

    float2 ao_uv() { return uvs.xy; }
    float2 projection_uv() { return uvs.zw; }
};

Varyings main_vs(uint vertex_ID : SV_VertexID) {
    Varyings output;
    // Draw triangle: {-1, -3}, { -1, 1 }, { 3, 1 }
    output.position.x = vertex_ID == 2 ? 3 : -1;
    output.position.y = vertex_ID == 0 ? -3 : 1;
    output.position.zw = float2(1, 1);

    output.uvs.xy = output.position.xy * 0.5 + 0.5;
    output.uvs.y = 1.0 - output.uvs.y;
    output.uvs.zw = (output.uvs.xy * ao_buffer_size - g_buffer_to_ao_index_offset) * recip_g_buffer_viewport_size;

    return output;
}

// ------------------------------------------------------------------------------------------------
// Depth conversion pixel shader.
// ------------------------------------------------------------------------------------------------

float linearize_depth_ps(Varyings input) : SV_TARGET {
    float z_over_w = depth_tex[input.position.xy].r;
    if (z_over_w == 1.0)
        return depth_far_plane_sentinel;

    float linear_depth = 1.0f / (z_over_w * scene_vars.inverted_projection_matrix._m23 + scene_vars.inverted_projection_matrix._m33);
    return linear_depth;
}

// ------------------------------------------------------------------------------------------------
// Bilateral blur.
// ------------------------------------------------------------------------------------------------

namespace BilateralBlur {

Texture2D normal_tex : register(t0);
Texture2D depth_tex : register(t1);
Texture2D ao_tex : register(t2);

cbuffer per_filter_constants : register(b2) {
    int per_pass_support;
    int __padding;
    int2 axis;
}

void sample_ao(int2 g_buffer_index, float3 normal, float plane_d, inout float summed_ao, inout float ao_weight) {
    // Normal weight
    float3 sample_normal = decode_ss_octahedral_normal(normal_tex[g_buffer_index].xy);
    float cos_theta = dot(sample_normal, normal);
    float weight = exp(-pow2(1.0f - cos_theta) * recip_double_normal_variance);

    // Plane fitting weight
    float sample_depth = depth_tex[g_buffer_index].r;
    float2 uv = (g_buffer_index + 0.5f) * recip_g_buffer_viewport_size;
    float3 sample_position = perspective_position_from_linear_depth(sample_depth, uv, scene_vars.inverted_projection_matrix);
    float distance_to_plane = dot(normal, sample_position) + plane_d;
    weight *= exp(-pow2(distance_to_plane) * recip_double_plane_variance);

    weight += 0.00001;

    summed_ao += weight * ao_tex[g_buffer_index + g_buffer_to_ao_index_offset].r;
    ao_weight += weight;
}

interface IFilter {
    void apply(int2 g_buffer_index, float3 view_normal, float plane_d, inout float summed_ao, inout float ao_weight);
};

float4 filter_input(Varyings input, IFilter filter) {
    int2 g_buffer_index = input.position.xy - g_buffer_to_ao_index_offset;
    float3 view_normal = decode_ss_octahedral_normal(normal_tex[g_buffer_index].xy);
    float depth = depth_tex[g_buffer_index].r;

    // No occlusion on the far plane.
    if (depth == depth_far_plane_sentinel)
        return float4(1, 0, 0, 0);

    float3 view_position = perspective_position_from_linear_depth(depth, input.projection_uv(), scene_vars.inverted_projection_matrix);
    float plane_d = -dot(view_position, view_normal);

    float center_ao = 0.0f;
    float center_weight = 0.0f;
    sample_ao(g_buffer_index, view_normal, plane_d, center_ao, center_weight); // TODO Can be inlined, just need to compute the weight, which should be pow2(exp(-0)) I guess.

    float border_ao = 0.0f;
    float border_weight = 0.0f;
    filter.apply(g_buffer_index, view_normal, plane_d, border_ao, border_weight);

    // Ensure that we perform at least some filtering in areas with high frequency geometry.
    if (border_weight < 2.0 * center_weight) {
        float weight_scale = 2.0 * center_weight * rcp(border_weight);
        border_ao *= weight_scale;
        border_weight = 2.0 * center_weight;
    }

    return float4((center_ao + border_ao) * rcp(center_weight + border_weight), 0, 0, 0);
}

class BoxFilter : IFilter {
    void apply(int2 g_buffer_index, float3 view_normal, float plane_d, inout float summed_ao, inout float ao_weight) {
        sample_ao(g_buffer_index + int2(-per_pass_support,  per_pass_support), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2(                0,  per_pass_support), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2( per_pass_support,  per_pass_support), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2(-per_pass_support,                 0), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2( per_pass_support,                 0), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2(-per_pass_support, -per_pass_support), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2(                0, -per_pass_support), view_normal, plane_d, summed_ao, ao_weight);
        sample_ao(g_buffer_index + int2( per_pass_support, -per_pass_support), view_normal, plane_d, summed_ao, ao_weight);
    }
};

float4 box_filter_ps(Varyings input) : SV_TARGET {
    BoxFilter filter;
    return filter_input(input, filter);
}

class CrossFilter : IFilter {
    void apply(int2 g_buffer_index, float3 view_normal, float plane_d, inout float summed_ao, inout float ao_weight) {
        for (int i = 1; i <= per_pass_support; ++i) {
            // TODO gaussian/tent filter
            sample_ao(g_buffer_index + i * axis, view_normal, plane_d, summed_ao, ao_weight);
            sample_ao(g_buffer_index - i * axis, view_normal, plane_d, summed_ao, ao_weight);
        }
    }
};

float4 cross_filter_ps(Varyings input) : SV_TARGET {
    CrossFilter filter;
    return filter_input(input, filter);
}

} // NS BilateralBlur

// ------------------------------------------------------------------------------------------------
// Alchemy pixel shader.
// ------------------------------------------------------------------------------------------------

float4 alchemy_ps(Varyings input) : SV_TARGET {

    static const float max_ss_radius = 0.25f;
    static const float rcp_max_ss_radius_squared = 1 / (max_ss_radius * max_ss_radius);

    float depth = depth_tex[input.position.xy - g_buffer_to_ao_index_offset].r;

    // No occlusion on the far plane.
    if (depth == depth_far_plane_sentinel)
        return float4(1, 0, 0, 0);

    // Setup sampling
    uint rng_offset = RNG::evenly_distributed_2D_seed(input.position.xy);
    float sample_pattern_rotation_angle = rng_offset * (TWO_PI / 4294967296.0f); // Scale a uint to the range [0, 2 * PI[
    float2x2 sample_pattern_rotation = generate_rotation_matrix(sample_pattern_rotation_angle);

    float3 view_normal = decode_ss_octahedral_normal(normal_tex[input.position.xy - g_buffer_to_ao_index_offset].xy);
    float pixel_bias = depth * bias * (1.0f - pow2(pow2(pow2(view_normal.z))));
    float3 view_position = perspective_position_from_linear_depth(depth, input.projection_uv(), scene_vars.inverted_projection_matrix) + view_normal * pixel_bias;

    // Compute screen space radius.
    float3 border_view_position = view_position + float3(world_radius, 0, 0);
    float border_u = u_coord_from_view_position(border_view_position);
    float ss_radius = border_u - input.projection_uv().x;
    float clamped_ss_radius = min(max_ss_radius, ss_radius);
    float recip_radius_scale = clamped_ss_radius * rcp(ss_radius);

    // Scale the view normal results in a scaled occlusion, which in turn accounts for the intensity change that comes from scaling the screen radius.
    float3 scaled_view_normal = view_normal * recip_radius_scale;

    // Determine occlusion
    float occlusion = 0.0f;
    for (int i = 0; i < sample_count; ++i) {
        float2 uv_offset = mul(uv_offsets[i] * ss_radius, sample_pattern_rotation);
        float2 sample_uv = input.projection_uv() + uv_offset;

        // Resample if sample is outside g-buffer.
        if (sample_uv.x < 0.0 || sample_uv.x > 1.0) sample_uv.x = sample_uv.x < 0.0 ? frac(-sample_uv.x) : 1.0f - frac(sample_uv.x);
        if (sample_uv.y < 0.0 || sample_uv.y > 1.0) sample_uv.y = sample_uv.y < 0.0 ? frac(-sample_uv.y) : 1.0f - frac(sample_uv.y);

        uv_offset = sample_uv - input.projection_uv();
        float uv_length_squared = dot(uv_offset, uv_offset); // Use original uv_offset instead of the resampled one
        float mip_level = max_depth_filtered_mipmap * sqrt(uv_length_squared * rcp_max_ss_radius_squared); // TODO Inline max radius squared.
        float depth_i = depth_tex.SampleLevel(trilinear_sampler, sample_uv, mip_level).r;
        float3 view_position_i = perspective_position_from_linear_depth(depth_i, sample_uv, scene_vars.inverted_projection_matrix);
        float3 v_i = view_position_i - view_position;

        // Equation 10
        occlusion += max(0, dot(v_i, scaled_view_normal)) * rcp(dot(v_i, v_i) + 0.0001f);
    }

    float a = 1 - intensity_over_sample_count * occlusion;
    a = pow(max(0.0, a), falloff);

    // Fade out if radius is less than two pixels.
    float pixel_width = g_buffer_size.x * ss_radius;
    a = lerp(1, a, saturate(pixel_width * 0.5f));

    return float4(a, 0, 0, 0);
}