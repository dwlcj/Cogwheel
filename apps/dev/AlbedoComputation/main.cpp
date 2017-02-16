// Albedo or directional-hemispherical reflectance computation.
// ---------------------------------------------------------------------------
// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#include <OptiXRenderer/Shading/BSDFs/Burley.h>
#include <OptiXRenderer/Shading/BSDFs/GGX.h>
#include <OptiXRenderer/Shading/BSDFs/OrenNayar.h>
#include <OptiXRenderer/Shading/ShadingModels/DefaultShading.h>
#include <OptiXRenderer/RNG.h>

#include <Cogwheel/Assets/Image.h>
#include <Cogwheel/Core/Array.h>
#include <Cogwheel/Math/Utils.h>

#include <StbImageWriter/StbImageWriter.h>

#include <fstream>

using namespace Cogwheel;
using namespace Cogwheel::Assets;
using namespace optix;
using namespace OptiXRenderer;
using namespace OptiXRenderer::Shading::BSDFs;
using namespace OptiXRenderer::Shading::ShadingModels;

typedef BSDFSample(*SampleRoughBSDF)(const float3& tint, float roughness, const float3& wo, float2 random_sample);

double estimate_rho(float3 wo, float roughness, unsigned int sample_count, SampleRoughBSDF sample_rough_BSDF) {

    const float3 tint = make_float3(1.0f, 1.0f, 1.0f);

    Core::Array<double> throughput = Core::Array<double>(sample_count);
    for (unsigned int s = 0; s < sample_count; ++s) {
        float2 rng_sample = RNG::sample02(s);
        BSDFSample sample = sample_rough_BSDF(tint, roughness, wo, rng_sample);
        if (is_PDF_valid(sample.PDF))
            throughput[s] = sample.weight.x * sample.direction.z / sample.PDF;
        else
            throughput[s] = 0.0;
    }

    return Math::sort_and_pairwise_summation(throughput.begin(), throughput.end()) / sample_count;
}

Image estimate_rho(unsigned int width, unsigned int height, unsigned int sample_count, SampleRoughBSDF sample_rough_BSDF) {
    Image rho_image = Images::create("rho", PixelFormat::RGB_Float, 1.0f, Math::Vector2ui(width, height));
    Math::RGB* rho_image_pixels = (Math::RGB*)rho_image.get_pixels();
    
    for (int y = 0; y < int(height); ++y) {
        float roughness = fmaxf(0.000001f, y / float(height - 1u));
        #pragma omp parallel for
        for (int x = 0; x < int(width); ++x) {
            float NdotV = (x + 0.5f) / float(width);
            float3 wo = make_float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
            double rho = estimate_rho(wo, roughness, sample_count, sample_rough_BSDF);
            rho_image_pixels[x + y * width] = Math::RGB(float(rho));
        }
    }

    return rho_image;
}

std::string format_float(float v) {
    std::ostringstream out;
    out << v;
    if (out.str().length() == 1)
        out << ".0f";
    else
        out << "f";
    return out.str();
}

template <int ElementDimensions>
void output_brdf(Image image, const std::string& filename, const std::string& data_name, const std::string& description) {
    
    unsigned int width = image.get_width();
    unsigned int height = image.get_height();
    Math::RGB* image_pixels = (Math::RGB*)image.get_pixels();

    std::string ifdef_name = data_name;
    for (int s = 0; s < ifdef_name.length(); ++s)
        ifdef_name[s] = toupper(ifdef_name[s]);

    std::ofstream out_header(filename);
    out_header <<
        "// " << description << "\n"
        "// ---------------------------------------------------------------------------\n"
        "// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors\n"
        "//\n"
        "// This program is open source and distributed under the New BSD License. See\n"
        "// LICENSE.txt for more detail.\n"
        "// ---------------------------------------------------------------------------\n"
        "// Generated by AlbedoComputation application.\n"
        "// ---------------------------------------------------------------------------\n"
        "\n"
        "#ifndef _COGWHEEL_ASSETS_SHADING_" << ifdef_name << "_RHO_H\n"
        "#define _COGWHEEL_ASSETS_SHADING_" << ifdef_name << "_RHO_H\n"
        "\n"
        "#include <Cogwheel/Math/Vector.h>\n"
        "\n"
        "namespace Cogwheel {\n"
        "namespace Assets {\n"
        "namespace Shading {\n"
        "\n";
    if (ElementDimensions > 1)
        out_header << "using namespace Cogwheel::Math;\n\n";
    out_header << "const unsigned int " << data_name << "_angle_sample_count = " << width << "u;\n"
        "const unsigned int " << data_name << "_roughness_sample_count = " << height << "u;\n"
        "\n";
    if (ElementDimensions == 1)
        out_header << "static const float " << data_name << "_rho[] = {\n";
    else
        out_header << "static const Vector" << ElementDimensions << "f " << data_name << "_rho[] = {\n";

    for (int y = 0; y < int(height); ++y) {
        float roughness = y / float(height - 1u);
        out_header << "    // Roughness " << roughness << "\n";
        out_header << "    ";
        for (int x = 0; x < int(width); ++x) {
            Math::RGB& rho = image_pixels[x + y * width];
            if (ElementDimensions == 1)
                out_header << format_float(rho.r) << ", ";
            else if (ElementDimensions == 2)
                out_header << "Vector2f(" << format_float(rho.r) << ", " << format_float(rho.g) << "), ";
            else if (ElementDimensions == 3)
                out_header << "Vector3f(" << format_float(rho.r) << ", " << format_float(rho.g) << ", " << format_float(rho.b) << "), ";
        }
        out_header << "\n";
    }

    out_header <<
        "};\n"
        "\n"
        "} // NS Shading\n"
        "} // NS Assets\n"
        "} // NS Cogwheel\n"
        "\n"
        "#endif // _COGWHEEL_ASSETS_SHADING_" << ifdef_name << "_RHO_H\n";

    out_header.close();
}

int main(int argc, char** argv) {
    printf("Albedo Computation\n");

    std::string output_dir = argc >= 2? argv[1] : std::string(COGWHEEL_SHADING_DIR);
    printf("output_dir: %s\n", output_dir.c_str());

    const unsigned int width = 128, height = 128, sample_count = 4096;

    Images::allocate(1);

    { // Default shading albedo.

        // Compute the directional-hemispherical reflectance function, albedo, by monte carlo integration and store the result in a texture and as an array in a header file.
        // The diffuse and specular components are separated by tinting the diffuse layer with green and keeping the specular layer white.
        // The albedo is computed via monte arlo integration by assuming that the material is lit by a uniform infinitely far away area light with an intensity of one.
        // As the base material is green it has no contribution to the red and blue channels, which means that these contain the albedo of the specular component.
        // The green channel contains the contribution of both the specular and diffuse components and the diffuse contribution alone can be found by subtracting the specular contribution from the green channel.
        // Notes
        // * Fresnel base reflectivity is set to zero. This is completely unrealistic, but gives us the largest possible range between full diffuse and full specular.

        // Specular material.
        Material material_params;
        material_params.tint = optix::make_float3(1.0f, 0.0f, 0.0f);
        material_params.metallic = 0.0f;
        material_params.specularity = 0.0f;

        Image rho = Images::create("rho", PixelFormat::RGB_Float, 1.0f, Math::Vector2ui(width, height));
        Math::RGB* rho_pixels = (Math::RGB*)rho.get_pixels();

        for (int y = 0; y < int(height); ++y) {
            material_params.roughness = y / float(height - 1u);
            #pragma omp parallel for
            for (int x = 0; x < int(width); ++x) {

                float NdotV = (x + 0.5f) / float(width);
                float3 wo = make_float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);

                DefaultShading material = DefaultShading(material_params);

                Core::Array<double> specular_throughput = Core::Array<double>(sample_count);
                Core::Array<double> total_throughput = Core::Array<double>(sample_count);
                for (unsigned int s = 0; s < sample_count; ++s) {

                    float3 rng_sample = make_float3(RNG::sample02(s), float(s) / float(sample_count));
                    BSDFSample sample = material.sample_all(wo, rng_sample);
                    if (is_PDF_valid(sample.PDF)) {
                        total_throughput[s] = sample.weight.x * sample.direction.z / sample.PDF;
                        specular_throughput[s] = sample.weight.y * sample.direction.z / sample.PDF;
                    } else
                        total_throughput[s] = specular_throughput[s] = 0.0;
                }

                double specular_rho = Math::sort_and_pairwise_summation(specular_throughput.begin(), specular_throughput.end()) / sample_count;
                double total_rho = Math::sort_and_pairwise_summation(total_throughput.begin(), total_throughput.end()) / sample_count;
                double diffuse_rho = total_rho - specular_rho;
                rho_pixels[x + y * width] = Math::RGB(float(diffuse_rho), float(specular_rho), 0.0f);
            }
        }

        // Store.
        StbImageWriter::write(output_dir + "DefaultShadingRho.png", rho);
        output_brdf<2>(rho, output_dir + "DefaultShadingRho.h", "default_shading",
            "Directional-hemispherical reflectance for default shaded material.");
    }

    { // Compute Burley rho.

        Image rho = estimate_rho(width, height, sample_count, Burley::sample);

        // Store.
        StbImageWriter::write(output_dir + "BurleyRho.png", rho);
        output_brdf<1>(rho, output_dir + "BurleyRho.h", "burley", "Directional-hemispherical reflectance for Burley.");
    }

    { // Compute OrenNayar rho.

        Image rho = estimate_rho(width, height, sample_count, OrenNayar::sample);

        // Store.
        StbImageWriter::write(output_dir + "OrenNayarRho.png", rho);
        output_brdf<1>(rho, output_dir + "OrenNayarRho.h", "oren_nayar", "Directional-hemispherical reflectance for OrenNayar.");
    }

    { // Compute GGX rho.

        static auto sample_ggx = [](const float3& tint, float roughness, const float3& wo, float2 random_sample) -> BSDFSample {
            float alpha = GGX::alpha_from_roughness(roughness);
            return GGX::sample(tint, alpha, wo, random_sample);
        };

        Image rho = estimate_rho(width, height, sample_count, sample_ggx);

        // Store.
        StbImageWriter::write(output_dir + "GGXRho.png", rho);
        output_brdf<1>(rho, output_dir + "GGXRho.h", "GGX", "Directional-hemispherical reflectance for GGX.");
    }

    { // Compute GGX with fresnel rho.

        static auto sample_ggx_with_fresnel = [](const float3& tint, float roughness, const float3& wo, float2 random_sample) -> BSDFSample {
            float alpha = GGX::alpha_from_roughness(roughness);
            BSDFSample sample = GGX::sample(tint, alpha, wo, random_sample);
            float3 halfway = normalize(wo + sample.direction);
            sample.weight *= schlick_fresnel(0.0f, dot(wo, halfway));
            return sample;
        };

        Image rho = estimate_rho(width, height, sample_count, sample_ggx_with_fresnel);

        // Store.
        StbImageWriter::write(output_dir + "GGXWithFresnelRho.png", rho);
        output_brdf<1>(rho, output_dir + "GGXWithFresnelRho.h", "GGX_with_fresnel",
            "Directional-hemispherical reflectance for GGX with fresnel factor.");
    }

    return 0;
}
