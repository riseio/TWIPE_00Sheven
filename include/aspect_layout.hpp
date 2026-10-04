#ifndef TWINE_ASPECT_LAYOUT_HPP
#define TWINE_ASPECT_LAYOUT_HPP

namespace twine::aspect {

inline constexpr float portal_projection_coefficient(
    float authored_coefficient,
    float horizontal_scale
) {

    return horizontal_scale > 0.0f
        ? authored_coefficient / horizontal_scale
        : authored_coefficient;
}

}

#endif
