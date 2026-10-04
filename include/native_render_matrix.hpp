#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace twine::render {
using Matrix = std::array<float, 16>;

inline constexpr Matrix identity_matrix() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

inline void validate_matrix(const Matrix& matrix) {
    for (float value : matrix) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("Nonfinite native render matrix");
        }
    }
}

inline void separate_camera_scale(Matrix& projection, Matrix& view) {
    validate_matrix(projection);
    validate_matrix(view);
    const float scale = view[15];
    if (view[3] != 0 || view[7] != 0 || view[11] != 0 || scale == 0) {
        throw std::runtime_error("Native camera view is not affine");
    }
    for (float& value : view) { value /= scale; }
    for (float& value : projection) { value *= scale; }
    validate_matrix(projection);
    validate_matrix(view);
}

inline Matrix multiply_matrix(const Matrix& left, const Matrix& right) {
    Matrix result{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            double value = 0;
            for (size_t k = 0; k < 4; ++k) {
                value += double(left[row * 4 + k]) * right[k * 4 + column];
            }
            result[row * 4 + column] = float(value);
        }
    }
    validate_matrix(result);
    return result;
}

inline Matrix inverse_affine_matrix(const Matrix& matrix) {
    validate_matrix(matrix);
    if (matrix[3] != 0 || matrix[7] != 0 || matrix[11] != 0 || matrix[15] != 1) {
        throw std::runtime_error("Native view correction is not affine");
    }
    const double a = matrix[0], b = matrix[1], c = matrix[2];
    const double d = matrix[4], e = matrix[5], f = matrix[6];
    const double g = matrix[8], h = matrix[9], i = matrix[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(determinant) || determinant == 0) {
        throw std::runtime_error("Native camera view is singular");
    }
    auto inverse = identity_matrix();
    const std::array<double, 9> cofactors{
        e * i - f * h, c * h - b * i, b * f - c * e,
        f * g - d * i, a * i - c * g, c * d - a * f,
        d * h - e * g, b * g - a * h, a * e - b * d};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            inverse[row * 4 + column] = float(cofactors[row * 3 + column] / determinant);
        }
    }
    for (size_t column = 0; column < 3; ++column) {
        double translation = 0;
        for (size_t k = 0; k < 3; ++k) {
            translation -= double(matrix[12 + k]) * inverse[k * 4 + column];
        }
        inverse[12 + column] = float(translation);
    }
    validate_matrix(inverse);
    return inverse;
}
}
