// Cogwheel ray.
// ----------------------------------------------------------------------------
// Copyright (C) 2015, Cogwheel. See AUTHORS.txt for authors
//
// This program is open source and distributed under the New BSD License. See
// LICENSE.txt for more detail.
// ----------------------------------------------------------------------------

#ifndef _COGWHEEL_MATH_RAY_H_
#define _COGWHEEL_MATH_RAY_H_

#include <Cogwheel/Math/Vector.h>

#include <cstring>
#include <sstream>

namespace Cogwheel {
namespace Math {

//----------------------------------------------------------------------------
// Implementation of a 3 dimensional ray.
//----------------------------------------------------------------------------
struct Ray final {
public:
    //*************************************************************************
    // Public members
    //*************************************************************************
    Vector3f origin;
    Vector3f direction;

    Ray() = default;
    Ray(Vector3f origin, Vector3f direction)
        : origin(origin), direction(direction) { }

    //*************************************************************************
    // Comparison operators.
    //*************************************************************************
    inline bool operator==(Ray rhs) const {
        return memcmp(this, &rhs, sizeof(rhs)) == 0;
    }
    inline bool operator!=(Ray rhs) const {
        return memcmp(this, &rhs, sizeof(rhs)) != 0;
    }

    inline Vector3f position_at(float t) const {
        return origin + direction * t;
    }

    inline std::string to_string() const {
        std::ostringstream out;
        out << "[origin: " << origin << ", direction: " << direction << "]";
        return out.str();
    }
};

} // NS Math
} // NS Cogwheel

// Convenience function that appends a ray's string representation to an ostream.
inline std::ostream& operator<<(std::ostream& s, Cogwheel::Math::Ray v) {
    return s << v.to_string();
}

#endif // _COGWHEEL_MATH_RAY_H_