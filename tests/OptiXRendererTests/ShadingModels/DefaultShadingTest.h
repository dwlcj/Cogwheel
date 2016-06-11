// Test OptiXRenderer's default shading model.
// ---------------------------------------------------------------------------
// Copyright (C) 2015-2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#ifndef _OPTIXRENDERER_SHADING_MODEL_DEFAULT_TEST_H_
#define _OPTIXRENDERER_SHADING_MODEL_DEFAULT_TEST_H_

#include <Utils.h>

#include <Cogwheel/Math/Utils.h>

#include <OptiXRenderer/RNG.h>
#include <OptiXRenderer/Shading/ShadingModels/DefaultShading.h>
#include <OptiXRenderer/Utils.h>

#include <gtest/gtest.h>

namespace OptiXRenderer {

Material gold_parameters() {
    Material gold_params;
    gold_params.base_tint = optix::make_float3(1.0f, 0.766f, 0.336f);
    gold_params.base_roughness = 0.02f;
    gold_params.metallic = 1.0f;
    gold_params.specularity = 0.02f; // Irrelevant when metallic is 1.
    return gold_params;
}

Material plastic_parameters() {
    Material plastic_params;
    plastic_params.base_tint = optix::make_float3(0.02f, 0.27f, 0.33f);
    plastic_params.base_roughness = 0.7f;
    plastic_params.metallic = 0.0f;
    plastic_params.specularity = 0.02f;
    return plastic_params;
}

GTEST_TEST(DefaultShadingModel, power_conservation) {
    using namespace Shading::ShadingModels;
    using namespace optix;

    const unsigned int MAX_SAMPLES = 4096u;
    
    // A nearly white material to stress test power_conservation.
    // We do not use a completely white material, as monte carlo sampling and 
    // floating point errors makes it impossible to guarantee white or less.
    Material material_params;
    material_params.base_tint = optix::make_float3(0.95f, 0.95f, 0.95f);
    material_params.base_roughness = 0.7f;
    material_params.metallic = 0.0f;
    material_params.specularity = 0.02f;
    DefaultShading material = DefaultShading(material_params);
    RNG::LinearCongruential rng;
    rng.seed(256237u);
    
    for (int i = 0; i < 10; ++i) {
        const float3 wo = normalize(make_float3(float(i), 0.0f, 1.001f - float(i) * 0.1f));
        float ws[MAX_SAMPLES];
        for (unsigned int s = 0u; s < MAX_SAMPLES; ++s) {
            BSDFSample sample = material.sample_all(wo, rng.sample3f());
            if (is_PDF_valid(sample.PDF))
                ws[s] = sample.weight.x * sample.direction.z / sample.PDF; // f * ||cos_theta|| / pdf
            else
                ws[s] = 0.0f;
        }

        float average_w = Cogwheel::Math::sort_and_pairwise_summation(ws, ws + MAX_SAMPLES) / float(MAX_SAMPLES);
        EXPECT_LE(average_w, 1.0f);
    }
}

GTEST_TEST(DefaultShadingModel, Helmholtz_reciprocity) {
    using namespace Shading::ShadingModels;
    using namespace optix;

    const unsigned int MAX_SAMPLES = 128u;

    DefaultShading plastic_material = DefaultShading(plastic_parameters());
    RNG::LinearCongruential rng;
    rng.seed(256237u);

    for (int i = 0; i < 10; ++i) {
        const float3 wo = normalize(make_float3(float(i), 0.0f, 1.001f - float(i) * 0.1f));
        for (unsigned int s = 0u; s < MAX_SAMPLES; ++s) {
            BSDFSample sample = plastic_material.sample_all(wo, rng.sample3f());
            if (is_PDF_valid(sample.PDF)) {
                // Re-evaluate contribution from both directions to avoid 
                // floating point imprecission between sampling and evaluating.
                // (Yes, they can actually get quite high on smooth materials.)
                float3 f0 = plastic_material.evaluate(wo, sample.direction);
                float3 f1 = plastic_material.evaluate(sample.direction, wo);

                EXPECT_TRUE(almost_equal_eps(f0.x, f1.x, 0.0001f));
                EXPECT_TRUE(almost_equal_eps(f0.y, f1.y, 0.0001f));
                EXPECT_TRUE(almost_equal_eps(f0.z, f1.z, 0.0001f));
            }
        }
    }
}

static void default_shading_model_consistent_PDF_test(Shading::ShadingModels::DefaultShading& material) {
    using namespace optix;

    const float3 wo = normalize(make_float3(1.0f, 0.0f, 1.0f));
    RNG::LinearCongruential rng;
    rng.seed(256237u);

    for (unsigned int i = 0u; i < 64; ++i) {
        BSDFSample sample = material.sample_all(wo, rng.sample3f());
        if (is_PDF_valid(sample.PDF)) {
            float PDF = material.PDF(wo, sample.direction);
            EXPECT_TRUE(almost_equal_eps(sample.PDF, PDF, 0.0001f));
        }
    }
}

GTEST_TEST(DefaultShadingModel, consistent_PDF) {
    using namespace Shading::ShadingModels;

    // This test can only be performed with rough materials, as the PDF of smooth materials 
    // is highly sensitive to floating point precision.
    DefaultShading plastic_material = DefaultShading(plastic_parameters());
    default_shading_model_consistent_PDF_test(plastic_material);
}

GTEST_TEST(DefaultShadingModel, Fresnel) {
    using namespace Shading::ShadingModels;
    using namespace optix;

    { // Test that specular reflections on non-metals are white and incident reflections are diffuse.
        Material material_params;
        material_params.base_tint = make_float3(1.0f, 0.0f, 0.0f);
        material_params.base_roughness = 0.02f;
        material_params.metallic = 0.0f;
        material_params.specularity = 0.0f; // Testing specularity. Physically-based fubar value.
        DefaultShading material = DefaultShading(material_params);

        { // Test that indicent reflectivity is red.
            float3 wo = make_float3(0.0f, 0.0f, 1.0f);
            float3 weight = material.evaluate(wo, wo);
            EXPECT_GT(weight.x, 0.0f);
            EXPECT_FLOAT_EQ(weight.y, 0.0f);
            EXPECT_FLOAT_EQ(weight.z, 0.0f);
        }

        { // Test that specular reflectivity is white.
            float3 wo = normalize(make_float3(0.0f, 1.0f, 0.001f));
            float3 wi = normalize(make_float3(0.0f, -1.0f, 0.001f));
            float3 weight = material.evaluate(wo, wi);
            EXPECT_GT(weight.x, 0.0f);
            EXPECT_FLOAT_EQ(weight.x, weight.y);
            EXPECT_FLOAT_EQ(weight.x, weight.z);
        }
    }

    { // Test that specular reflections on metals are tinted.
        Material material_params = gold_parameters();
        DefaultShading material = DefaultShading(material_params);

        { // Test that indicent reflectivity is base tint scaled.
            float3 wo = make_float3(0.0f, 0.0f, 1.0f);
            float3 weight = material.evaluate(wo, wo);
            float scale = material_params.base_tint.x / weight.x;
            EXPECT_FLOAT_EQ(weight.x * scale, material_params.base_tint.x);
            EXPECT_FLOAT_EQ(weight.y * scale, material_params.base_tint.y);
            EXPECT_FLOAT_EQ(weight.z * scale, material_params.base_tint.z);
        }

        { // Test that grazing angle reflectivity is base tint scaled.
            float3 wo = normalize(make_float3(0.0f, 1.0f, 0.001f));
            float3 wi = normalize(make_float3(0.0f, -1.0f, 0.001f));
            float3 weight = material.evaluate(wo, wi);
            float scale = material_params.base_tint.x / weight.x;
            EXPECT_FLOAT_EQ(weight.x * scale, material_params.base_tint.x);
            EXPECT_FLOAT_EQ(weight.y * scale, material_params.base_tint.y);
            EXPECT_FLOAT_EQ(weight.z * scale, material_params.base_tint.z);
        }
    }
}

GTEST_TEST(DefaultShadingModel, sampling_variance) {
    using namespace Shading::ShadingModels;
    using namespace optix;

    // Test that sample_all_BSDFs has lower variance than sample_one_BSDF and that they converge to the same result'ish.

    const unsigned int MAX_SAMPLES = 2048 * 32;
    const float3 wo = normalize(make_float3(1.0f, 0.0f, 1.0f));
    RNG::LinearCongruential rng;
    
    Material material_params;
    material_params.base_tint = make_float3(0.5f, 0.5f, 0.5f);
    material_params.base_roughness = 0.9f;
    material_params.metallic = 0.0f;
    material_params.specularity = 0.2f;
    DefaultShading material = DefaultShading(material_params);

    rng.seed(256237u);
    double* ws = new double[MAX_SAMPLES];
    double* ws_squared = new double[MAX_SAMPLES];
    for (unsigned int i = 0u; i < MAX_SAMPLES; ++i) {
        BSDFSample sample = material.sample_one(wo, rng.sample3f());
        if (is_PDF_valid(sample.PDF)) {
            ws[i] = sample.weight.x * abs(sample.direction.z) / sample.PDF; // f * ||cos_theta|| / pdf
            ws_squared[i] = ws[i] * ws[i];
        } else
            ws_squared[i] = ws[i] = 0.0f;
    }
    
    double sample_one_mean = Cogwheel::Math::sort_and_pairwise_summation(ws, ws + MAX_SAMPLES) / double(MAX_SAMPLES);
    double sample_one_mean_squared = Cogwheel::Math::sort_and_pairwise_summation(ws_squared, ws_squared + MAX_SAMPLES) / double(MAX_SAMPLES);
    double sample_one_variance = sample_one_mean_squared - sample_one_mean * sample_one_mean;

    rng.seed(256237u);
    for (unsigned int i = 0u; i < MAX_SAMPLES; ++i) {
        BSDFSample sample = material.sample_all(wo, rng.sample3f());
        if (is_PDF_valid(sample.PDF)) {
            ws[i] = sample.weight.x * abs(sample.direction.z) / sample.PDF; // f * ||cos_theta|| / pdf
            ws_squared[i] = ws[i] * ws[i];
        } else
            ws_squared[i] = ws[i] = 0.0f;
    }

    double sample_all_mean = Cogwheel::Math::sort_and_pairwise_summation(ws, ws + MAX_SAMPLES) / double(MAX_SAMPLES);
    double sample_all_mean_squared = Cogwheel::Math::sort_and_pairwise_summation(ws_squared, ws_squared + MAX_SAMPLES) / double(MAX_SAMPLES);
    double sample_all_variance = sample_all_mean_squared - sample_all_mean * sample_all_mean;

    EXPECT_TRUE(almost_equal_eps(float(sample_one_mean), float(sample_all_mean), 0.001f));
    EXPECT_LT(sample_all_variance, sample_one_variance);

    delete[] ws;
    delete[] ws_squared;
}

} // NS OptiXRenderer

#endif // _OPTIXRENDERER_SHADING_MODEL_DEFAULT_TEST_H_