// Fitted Spherical pivot.
// ------------------------------------------------------------------------------------------------
// Copyright (C) 2017, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License.
// See LICENSE.txt for more detail.
// ------------------------------------------------------------------------------------------------

#ifndef _FIT_SPTD_PIVOT_H_
#define _FIT_SPTD_PIVOT_H_

#include <OptiXRenderer/Utils.h>

using namespace optix;
using namespace OptiXRenderer;

// pivot operator
// x: sample on the sphere
// xi: pivot
float3 pivot_transform(const float3& x, const float3& xi) {
    float3 tmp = x - xi;
    float3 cp1 = cross(x, xi);
    float3 cp2 = cross(tmp, cp1);
    float dp = dot(x, xi) - 1.0f;
    float qf = dp * dp + dot(cp1, cp1);

    return ((dp * tmp - cp2) / qf);
}

// evaluate the pdf with uniform original distribution
// x: point on the sphere
// xi: pivot
float pdf_uniform(const float3& x, const float3& xi) {
    float num = 1.0f - dot(xi, xi);
    float3 tmp = x - xi;
    float den = dot(tmp, tmp);
    float p = num / den;
    float jacobian = p * p;
    float pdf_ = jacobian / (4.0f * PIf);
    return pdf_;
}

// TODO Split into SPTD::Pivot and SPTD::PivotFit, where the amplitude is stored in the pivot for fitting.
// TODO Parameterize by distribution, fx uniform or cosine.
struct Pivot {

	// lobe amplitude
	float amplitude;

	// parameterization 
	float distance;
	float theta;
	
	// pivot position
    inline float3 position() const { return distance * make_float3(sinf(theta), 0.0f, cosf(theta)); }

	float eval(const float3& wi) const {
		return amplitude * pdf_uniform(wi, position());
	}

	float3 sample(const float U1, const float U2) const {
		const float sphere_theta = acosf(-1.0f + 2.0f*U1);
		const float sphere_phi = 2.0f*3.14159f * U2;
		const float3 sphere_sample = make_float3(sinf(sphere_theta) * cosf(sphere_phi), sinf(sphere_theta) * sinf(sphere_phi), -cosf(sphere_theta));
        return pivot_transform(sphere_sample, position());
	}

    /*
	float max_value() {
		float res = 0.0f;
		for(float U2 = 0.0f ; U2 <= 1.0f ; U2+=0.05f)
		    for(float U1 = 0.0f ; U1 <= 1.0f ; U1+=0.05f)
		    {
			    float3 L = sample(U1, U2);
			    float value = eval(L) / amplitude;
                res = std::max(value, res);
		    }
		return res;
	}

	float test_normalization() const {
		double sum = 0;
		float dtheta = 0.005f;
		float dphi = 0.005f;
		for(float theta = 0.0f ; theta <= 3.14159f ; theta+=dtheta)
		for(float phi = 0.0f ; phi <= 2.0f * 3.14159f ; phi+=dphi)
		{
			float3 L(cosf(phi)*sinf(theta), sinf(phi)*sinf(theta), cosf(theta));

			sum += sinf(theta) * eval(L);
		}
		sum *= dtheta * dphi;
		// cout << "Pivot normalization test: " << sum << endl;
		// cout << "Pivot normalization expected: " << amplitude << endl;
        return float(sum);
	}
    */
};

#endif // _FIT_SPTD_PIVOT_H_