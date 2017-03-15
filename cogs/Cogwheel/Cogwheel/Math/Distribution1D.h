// Cogwheel 1D distribution.
// ------------------------------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ------------------------------------------------------------------------------------------------

#ifndef _COGWHEEL_MATH_DISTRIBUTION1D_H_
#define _COGWHEEL_MATH_DISTRIBUTION1D_H_

#include <assert.h>

namespace Cogwheel {
namespace Math {

// ------------------------------------------------------------------------------------------------
// A 1D distribution of a discretized function.
// Future work.
// * SSE/AVX
// ------------------------------------------------------------------------------------------------
template <typename T>
struct Distribution1D final {

    const int m_element_count;
    T m_integral; // The integral of the function scaled to the domain [0, 1].
    T* m_CDF;

public:

    // --------------------------------------------------------------------------------------------
    // A single sample from  the distribution.
    // --------------------------------------------------------------------------------------------
    template <typename T>
    struct Sample {
        T index;
        float PDF;
    };

    //*********************************************************************************************
    // Constructors and destructors.
    //*********************************************************************************************
    template <typename U>
    Distribution1D(U* function, int element_count)
        : m_element_count(element_count), m_CDF(new T[m_element_count + 1]) {
        m_integral = compute_CDF(function, m_element_count, m_CDF);
    }

    template <typename U>
    Distribution1D(Distribution1D<U> other)
        : m_element_count(other.element_count), m_integral(other.m_integral)
        , m_CDF(new T[m_element_count + 1]) {
        for (int i = 0; i < m_element_count + 1; ++i)
            m_CDF[i] = T(other.m_CDF[i]);
    }

    ~Distribution1D() {
        delete[] m_CDF;
    }

    //*********************************************************************************************
    // Getters and setters.
    //*********************************************************************************************

    inline int get_element_count() const { return m_element_count; }
    inline T get_integral() const { return m_integral; }

    inline const T* const get_CDF() { return m_CDF; }
    inline int get_CDF_size() { return m_element_count + 1; }

    //*********************************************************************************************
    // Sampling.
    //*********************************************************************************************

private:
    static int binary_search (float random_sample, T* CDF, int element_count) {
        int lowerbound = 0;
        int upperbound = element_count;
        while (lowerbound + 1 != upperbound) {
            int middlebound = (lowerbound + upperbound) / 2;
            T cdf = CDF[middlebound];
            if (random_sample < cdf)
                upperbound = middlebound;
            else
                lowerbound = middlebound;
        }
        return lowerbound;
    }

public:

    T evaluate(int i) {
        return (m_CDF[i + 1] - m_CDF[i]) * m_element_count * m_integral;
    }

    T evaluate(float u) {
        int i = int(u * m_element_count);
        return evaluate(i);
    }

    Sample<int> sample_discrete(float random_sample) const {
        assert(0.0f <= random_sample && random_sample < 1.0f);

        int i = binary_search(random_sample, m_CDF, m_element_count);
        T PDF = m_CDF[i + 1] - m_CDF[i];
        return { i, float(PDF) };
    }

    Sample<float> sample_continuous(float random_sample) const {
        assert(0.0f <= random_sample && random_sample < 1.0f);

        int i = binary_search(random_sample, m_CDF, m_element_count);
        T cdf_at_i = m_CDF[i];
        T di = (random_sample - cdf_at_i) / (m_CDF[i + 1] - cdf_at_i); // Inverse lerp.

        T PDF = (m_CDF[i + 1] - m_CDF[i]) * m_element_count;

        return { float(i + di) / m_element_count, float(PDF) };
    }

    //*********************************************************************************************
    // CDF construction.
    //*********************************************************************************************

    template <typename U>
    static T compute_CDF(U* function, int element_count, T* CDF) {

        // Compute step function.
        CDF[0] = T(0.0);
        for (int i = 0; i < element_count; ++i)
            CDF[i + 1] = CDF[i] + T(function[i]);

        // Integral of the function.
        T integral = CDF[element_count] / element_count;

        // Normalize to get the CDF.
        for (int i = 1; i < element_count; ++i)
            CDF[i] /= CDF[element_count];
        CDF[element_count] = 1.0;

        return integral;
    }

};

} // NS Math
} // NS Cogwheel

#endif // _COGWHEEL_MATH_DISTRIBUTION2D_H_