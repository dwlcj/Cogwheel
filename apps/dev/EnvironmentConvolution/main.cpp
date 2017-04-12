// Convolute environment maps with a GGX distribution.
// ---------------------------------------------------------------------------
// Copyright (C) 2016, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ---------------------------------------------------------------------------

#include <Cogwheel/Assets/InfiniteAreaLight.h>
#include <Cogwheel/Math/Quaternion.h>
#include <Cogwheel/Math/RNG.h>

#include <StbImageLoader/StbImageLoader.h>
#include <StbImageWriter/StbImageWriter.h>
#include <TinyExr/TinyExr.h>

#include <omp.h>

#include <atomic>
#include <fstream>

using namespace Cogwheel;
using namespace Cogwheel::Assets;
using namespace Cogwheel::Math;

//==============================================================================
// GGX distribution.
//==============================================================================
namespace GGX {

struct Sample {
    Vector3f direction;
    float PDF;
};

inline float D(float alpha, float abs_cos_theta) {
    float alpha_sqrd = alpha * alpha;
    float cos_theta_sqrd = abs_cos_theta * abs_cos_theta;
    float tan_theta_sqrd = fmaxf(1.0f - cos_theta_sqrd, 0.0f) / cos_theta_sqrd;
    float cos_theta_cubed = cos_theta_sqrd * cos_theta_sqrd;
    float foo = alpha_sqrd + tan_theta_sqrd; // No idea what to call this.
    return alpha_sqrd / (PI<float>() * cos_theta_cubed * foo * foo);
}

inline float PDF(float alpha, float abs_cos_theta) {
    return D(alpha, abs_cos_theta) * abs_cos_theta;
}

inline Sample sample(float alpha, Vector2f random_sample) {
    float phi = random_sample.y * (2.0f * PI<float>());

    float tan_theta_sqrd = alpha * alpha * random_sample.x / (1.0f - random_sample.x);
    float cos_theta = 1.0f / sqrt(1.0f + tan_theta_sqrd);

    float r = sqrt(fmaxf(1.0f - cos_theta * cos_theta, 0.0f));

    Sample res;
    res.direction = Vector3f(cos(phi) * r, sin(phi) * r, cos_theta);
    res.PDF = PDF(alpha, cos_theta); // We have to be able to inline this to reuse some temporaries.
    return res;
}

} // NS GGX

// Computes the power heuristic of pdf1 and pdf2.
// It is assumed that pdf1 is always valid, i.e. not NaN.
// pdf2 is allowed to be NaN, but generally try to avoid it. :)
inline float power_heuristic(float pdf1, float pdf2) {
    pdf1 *= pdf1;
    pdf2 *= pdf2;
    float result = pdf1 / (pdf1 + pdf2);
    // This is where floating point math gets tricky!
    // If the mis weight is NaN then it can be caused by three things.
    // 1. pdf1 is so insanely high that pdf1 * pdf1 = infinity. In that case we end up with inf / (inf + pdf2^2) and return 1, unless pdf2 was larger than pdf1, i.e. 'more infinite :p', then we return 0.
    // 2. Conversely pdf2 can also be so insanely high that pdf2 * pdf2 = infinity. This is handled analogously to above.
    // 3. pdf2 can also be NaN. In this case the power heuristic is ill-defined and we return 0.
    return !isnan(result) ? result : (pdf1 > pdf2 ? 1.0f : 0.0f);
}

enum class SampleMethod {
    MIS, Light, BSDF
};

struct Options {
    SampleMethod sample_method;
    int sample_count;

    static Options parse(int argc, char** argv) {
        Options options = { SampleMethod::BSDF, 256 };

        // Skip the first two arguments, the application name and image path.
        for (int argument = 2; argument < argc; ++argument) {
            if (strcmp(argv[argument], "--mis-sampling") == 0 || strcmp(argv[argument], "-m") == 0)
                options.sample_method = SampleMethod::MIS;
            else if (strcmp(argv[argument], "--light-sampling") == 0 || strcmp(argv[argument], "-l") == 0)
                options.sample_method = SampleMethod::Light;
            else if (strcmp(argv[argument], "--bsdf-sampling") == 0 || strcmp(argv[argument], "-b") == 0)
                options.sample_method = SampleMethod::BSDF;
            else if (strcmp(argv[argument], "--sample-count") == 0 || strcmp(argv[argument], "-s") == 0)
                options.sample_count = atoi(argv[++argument]);
        }

        return options;
    }

    std::string to_string() const {
        std::ostringstream out;
        switch (sample_method) {
        case SampleMethod::MIS: out << "MIS sampling, "; break;
        case SampleMethod::Light: out << "Light sampling, "; break;
        case SampleMethod::BSDF: out << "BSDF sampling, "; break;
        }
        out << sample_count << " samples pr pixel.";
        return out.str();
    }
};

void print_usage() {
    char* usage =
        "usage EnvironmentConvolution <path/to/environment.ext>:\n"
        "  -h | --help: Show command line usage for EnvironmentConvolution.\n"
        "  -s | --sample-count. The number of samples pr pixel.\n"
        "  -m | --mis-sampling. Combine light and bsdf samples by multiple importance sampling.\n"
        "  -l | --light-sampling. Draw samples from the environment.\n"
        "  -b | --bsdf-sampling. Draw samples from the GGX distribution.\n";
    printf("%s", usage);
}

int main(int argc, char** argv) {
    printf("Environment convolution\n");

    if (argc == 1 || std::string(argv[1]).compare("-h") == 0 || std::string(argv[1]).compare("--help") == 0) {
        print_usage();
        return 0;
    }

    std::string image_file = std::string(argv[1]);

    std::string file_extension = std::string(image_file, image_file.length() - 4);
    // Check if the file format is supported.
    if (!(file_extension.compare(".bmp") == 0 ||
        file_extension.compare(".exr") == 0 ||
        file_extension.compare(".hdr") == 0 ||
        file_extension.compare(".png") == 0 ||
        file_extension.compare(".tga") == 0)) {
        printf("Unsupported file format: %s\nSupported formats are: bmp, exr, hdr, png and tga.\n", file_extension.c_str());
        return 2;
    }

    Options options = Options::parse(argc, argv);

    printf("Convolute '%s'\n", argv[1]);
    printf("  %s\n", options.to_string().c_str());

    Images::allocate(1);
    Textures::allocate(1);

    Image image;
    if (file_extension.compare(".exr") == 0)
        image = TinyExr::load(std::string(argv[1]));
    else
        image = StbImageLoader::load(std::string(argv[1]));

    if (!image.exists()) {
        printf("Could not load image: %s\n", image_file.c_str());
        return 1;
    }

    Textures::UID texture_ID = Textures::create2D(image.get_ID(), MagnificationFilter::Linear, MinificationFilter::Linear, WrapMode::Repeat, WrapMode::Clamp);
    InfiniteAreaLight* infinite_area_light = nullptr;
    std::vector<LightSample> light_samples = std::vector<LightSample>();
    if (options.sample_method != SampleMethod::BSDF) {
        infinite_area_light = new InfiniteAreaLight(texture_ID);
        light_samples.resize(options.sample_count * 8);
        for (int s = 0; s < light_samples.size(); ++s)
            light_samples[s] = infinite_area_light->sample(RNG::sample02(s));
    }

    Image output = Images::create("Convoluted image", PixelFormat::RGB_Float, 1.0f, Vector2ui(image.get_width(), image.get_height()));

    std::atomic_int finished_pixel_count;
    finished_pixel_count.store(0);
    for (int r = 0; r < 11; ++r) {
        float roughness = r / 10.0f;
        float alpha = fmaxf(0.00000000001f, roughness * roughness);

        std::vector<GGX::Sample> ggx_samples = std::vector<GGX::Sample>(options.sample_count);
        ggx_samples.resize(options.sample_count);
        #pragma omp parallel for schedule(dynamic, 16)
        for (int s = 0; s < ggx_samples.size(); ++s)
            ggx_samples[s] = GGX::sample(alpha, RNG::sample02(s));

        #pragma omp parallel for schedule(dynamic, 16)
        for (int i = 0; i < int(image.get_pixel_count()); ++i) {

            int x = i % image.get_width();
            int y = i / image.get_width();

            Vector2f up_uv = Vector2f((x + 0.5f) / image.get_width(), (y + 0.5f) / image.get_height());
            Vector3f up_vector = latlong_texcoord_to_direction(up_uv);
            Quaternionf up_rotation = Quaternionf::look_in(up_vector);

            RGB radiance = RGB::black();

            switch (options.sample_method) {
            case SampleMethod::MIS: {
                int bsdf_sample_count = options.sample_count / 2;
                int light_sample_count = options.sample_count - bsdf_sample_count;

                for (int s = 0; s < light_sample_count; ++s) {
                    LightSample sample = light_samples[(s + RNG::hash(i)) % light_samples.size()];
                    if (sample.PDF < 0.000000001f)
                        continue;

                    Vector3f local_direction = normalize(inverse_unit(up_rotation) * sample.direction_to_light);
                    float ggx_f = GGX::D(alpha, local_direction.z);
                    if (isnan(ggx_f))
                        continue;

                    float cos_theta = fmaxf(local_direction.z, 0.0f);
                    float mis_weight = power_heuristic(sample.PDF, GGX::PDF(alpha, local_direction.z));
                    radiance += sample.radiance * (mis_weight * ggx_f * cos_theta / sample.PDF);
                }

                for (int s = 0; s < bsdf_sample_count; ++s) {
                    GGX::Sample sample = GGX::sample(alpha, RNG::sample02(s));
                    if (sample.PDF < 0.000000001f)
                        continue;

                    sample.direction = normalize(up_rotation * sample.direction);
                    float mis_weight = power_heuristic(sample.PDF, infinite_area_light->PDF(sample.direction));
                    radiance += infinite_area_light->evaluate(sample.direction) * mis_weight;
                }

                // Account for the samples being split evenly between BSDF and light.
                radiance *= 2.0f;

                break;
            }
            case SampleMethod::Light:
                for (int s = 0; s < options.sample_count; ++s) {
                    LightSample sample = light_samples[s];
                    if (sample.PDF < 0.000000001f)
                        continue;

                    Vector3f local_direction = inverse_unit(up_rotation) * sample.direction_to_light;
                    float ggx_f = GGX::D(alpha, local_direction.z);
                    if (isnan(ggx_f))
                        continue;

                    float cos_theta = fmaxf(local_direction.z, 0.0f);
                    radiance += sample.radiance * ggx_f * cos_theta / sample.PDF;
                }
                break;
            case SampleMethod::BSDF:
                for (int s = 0; s < options.sample_count; ++s) {
                    const GGX::Sample& sample = ggx_samples[s];
                    Vector2f sample_uv = direction_to_latlong_texcoord(up_rotation * sample.direction);
                    radiance += sample2D(texture_ID, sample_uv).rgb();
                }
                break;
            }

            radiance /= float(options.sample_count);

            output.set_pixel(RGBA(radiance), Vector2ui(x, y));

            ++finished_pixel_count;
            if (omp_get_thread_num() == 0)
                printf("\rProgress: %.2f%%", 100.0f * float(finished_pixel_count) / (image.get_pixel_count() * 11.0f));
        }

        { // Output convoluted image.
            size_t dot_pos = image_file.find_last_of('.');
            std::string file_sans_extension = std::string(image_file, 0, dot_pos);
            std::string extension = std::string(image_file, dot_pos);
            std::ostringstream output_file;
            output_file << file_sans_extension << "_roughness_" << roughness << extension;
            if (extension.compare(".exr") == 0)
                TinyExr::store(output.get_ID(), output_file.str());
            else
                // TODO Flip parameter order.
                StbImageWriter::write(output_file.str(), output.get_ID());
        }
    }

    printf("\rProgress: 100.00%%\n");

    return 0;
}
