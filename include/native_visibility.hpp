#pragma once

#include "native_render_matrix.hpp"
#include <algorithm>
#include <limits>

namespace twine::render {
struct VisibilityRect {
    double left, right, top, bottom;
    bool empty() const { return left >= right || top >= bottom; }
    VisibilityRect intersect(const VisibilityRect& other) const {
        return {std::max(left, other.left), std::min(right, other.right),
            std::max(top, other.top), std::min(bottom, other.bottom)};
    }
    void include(const VisibilityRect& other) {
        if (other.empty()) { return; }
        if (empty()) { *this = other; return; }
        left = std::min(left, other.left); right = std::max(right, other.right);
        top = std::min(top, other.top); bottom = std::max(bottom, other.bottom);
    }
};

struct VisibilityViewport {
    double scaleX, scaleY, offsetX, offsetY;
};

struct VisibilityRange {
    double low = 0, high = 0;
    VisibilityRange() = default;
    VisibilityRange(double value) : low(value), high(value) {}
    VisibilityRange(double a, double b) : low(std::min(a, b)), high(std::max(a, b)) {}
    VisibilityRange operator+(VisibilityRange b) const { return {low + b.low, high + b.high}; }
    VisibilityRange operator-() const { return {-high, -low}; }
    VisibilityRange operator-(VisibilityRange b) const { return *this + -b; }
    VisibilityRange operator*(VisibilityRange b) const {
        return {std::min({low*b.low, low*b.high, high*b.low, high*b.high}),
            std::max({low*b.low, low*b.high, high*b.low, high*b.high})};
    }
    VisibilityRange operator/(VisibilityRange b) const {
        if (b.low <= 0 && b.high >= 0) {
            return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        }
        return *this * VisibilityRange(1 / b.high, 1 / b.low);
    }
};

class VisibilitySweep {
    std::array<std::array<double, 16>, 3> clip{};
    std::array<VisibilityRange, 3> eye{};
    VisibilityRect viewport{};
    VisibilityViewport mapping{};
    double radiusScale = 0;
public:
    VisibilitySweep(const Matrix& previousView, const Matrix& currentView,
            const Matrix& previousLens, const Matrix& currentLens, VisibilityRect bounds)
        : VisibilitySweep(previousView, currentView, previousLens, currentLens, bounds,
            {(bounds.right-bounds.left)*0.5, (bounds.bottom-bounds.top)*0.5,
                (bounds.right+bounds.left)*0.5, (bounds.bottom+bounds.top)*0.5}) {}

    VisibilitySweep(const Matrix& previousView, const Matrix& currentView,
            const Matrix& previousLens, const Matrix& currentLens, VisibilityRect bounds,
            VisibilityViewport screen) : viewport(bounds), mapping(screen) {
        if (!std::isfinite(screen.scaleX) || !std::isfinite(screen.scaleY) ||
                !std::isfinite(screen.offsetX) || !std::isfinite(screen.offsetY) ||
                screen.scaleX <= 0 || screen.scaleY <= 0 || bounds.empty()) {
            throw std::runtime_error("Invalid native visibility viewport mapping");
        }
        radiusScale = 32 * (std::max(std::abs(previousLens[0]), std::abs(currentLens[0])) *
            screen.scaleX + std::max(std::abs(previousLens[5]), std::abs(currentLens[5])) * screen.scaleY);
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                for (size_t k = 0; k < 4; ++k) {
                    clip[0][r*4+c] += double(previousView[r*4+k]) * previousLens[k*4+c];
                    clip[1][r*4+c] += (double(previousView[r*4+k]) * currentLens[k*4+c] +
                        double(currentView[r*4+k]) * previousLens[k*4+c]) * 0.5;
                    clip[2][r*4+c] += double(currentView[r*4+k]) * currentLens[k*4+c];
                }
            }
        }

        std::array<VisibilityRange, 16> v;
        for (size_t i = 0; i < 16; ++i) { v[i] = {previousView[i], currentView[i]}; }
        const auto a=v[0], b=v[1], c=v[2], d=v[4], e=v[5], f=v[6], g=v[8], h=v[9], i=v[10];
        const auto det = a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
        const std::array<VisibilityRange, 9> inverse{
            (e*i-f*h)/det, (c*h-b*i)/det, (b*f-c*e)/det,
            (f*g-d*i)/det, (a*i-c*g)/det, (c*d-a*f)/det,
            (d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det};
        for (size_t column = 0; column < 3; ++column) {
            if (det.low <= 0 && det.high >= 0) {
                eye[column] = {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            }
            else {
                for (size_t k = 0; k < 3; ++k) { eye[column] = eye[column] - v[12+k]*inverse[k*3+column]; }
                eye[column] = eye[column] / VisibilityRange(64);
            }
        }
    }

    double planeMaximum(const std::array<float, 4>& plane) const {
        VisibilityRange distance(plane[3]);
        for (size_t i = 0; i < 3; ++i) {
            if (plane[i] == 0) { continue; }
            if (!std::isfinite(eye[i].low) || !std::isfinite(eye[i].high)) {
                return std::numeric_limits<float>::max();
            }
            distance = distance + eye[i]*VisibilityRange(plane[i]);
        }
        return distance.high;
    }

    double distanceMinimum(const std::array<float, 3>& point) const {
        double squared = 0;
        for (size_t i = 0; i < 3; ++i) {
            const double delta = std::max({eye[i].low - point[i], point[i] - eye[i].high, 0.0});
            squared += delta * delta;
        }
        return std::sqrt(squared);
    }

    bool sphere(const std::array<float, 3>& previous, const std::array<float, 3>& current,
            float radius, VisibilityRect parent) const {
        parent = parent.intersect(viewport);
        if (parent.empty()) { return false; }
        const double cx = mapping.offsetX, cy = mapping.offsetY;
        const double sx = mapping.scaleX, sy = mapping.scaleY;
        const double left = (parent.left - cx) / sx, right = (parent.right - cx) / sx;
        const double top = (cy - parent.top) / sy, bottom = (cy - parent.bottom) / sy;

        for (const std::array<double, 4> plane : {
                std::array<double, 4>{1, 0, 0, -left}, {-1, 0, 0, right},
                {0, 1, 0, -bottom}, {0, -1, 0, top}, {0, 0, 1, 1}, {0, 0, -1, 1}}) {
            double maximum = -std::numeric_limits<double>::infinity();
            for (const auto& control : clip) {
                double upper = 0, magnitude = 1;
                for (size_t row = 0; row < 4; ++row) {
                    double coefficient = 0;
                    for (size_t column = 0; column < 4; ++column) {
                        coefficient += control[row * 4 + column] * plane[column];
                    }
                    const VisibilityRange coordinate = row == 3 ? VisibilityRange(1) :
                        VisibilityRange((std::min(previous[row], current[row]) - double(radius)) * 64,
                            (std::max(previous[row], current[row]) + double(radius)) * 64);
                    const auto term = coordinate * VisibilityRange(coefficient);
                    upper += term.high;
                    magnitude += std::max(std::abs(term.low), std::abs(term.high));
                }
                maximum = std::max(maximum, upper + magnitude * 32 * std::numeric_limits<float>::epsilon());
            }
            if (maximum < 0) { return false; }
        }
        return true;
    }

    double radiusPixelsMaximum(const std::array<float, 3>& previous,
            const std::array<float, 3>& current, float radius) const {
        double minimumW = std::numeric_limits<double>::infinity();
        for (const auto& control : clip) {
            VisibilityRange w(control[15]);
            for (size_t row = 0; row < 3; ++row) {
                w = w + VisibilityRange(double(previous[row]) * 64, double(current[row]) * 64) *
                    VisibilityRange(control[row * 4 + 3]);
            }
            minimumW = std::min(minimumW, w.low);
        }
        return minimumW <= 0 ? std::numeric_limits<double>::infinity() : radius * radiusScale / minimumW;
    }

    VisibilityRect triangle(const std::array<std::array<float, 3>, 3>& vertices,
            VisibilityRect parent) const {
        parent = parent.intersect(viewport);
        VisibilityRect result{1, 0, 1, 0};
        bool crossingEye = false, anyInFront = false;
        for (const auto& vertex : vertices) {
            std::array<VisibilityRange, 4> position;
            for (size_t c = 0; c < 4; ++c) {
                std::array<double, 3> control{};
                double magnitude = 1;
                for (size_t n = 0; n < 3; ++n) {
                    control[n] = clip[n][12+c];
                    magnitude += std::abs(control[n]);
                    for (size_t r = 0; r < 3; ++r) {
                        const double term = double(vertex[r])*64*clip[n][r*4+c];
                        control[n] += term; magnitude += std::abs(term);
                    }
                }

                const double error = magnitude * 32 * std::numeric_limits<float>::epsilon();
                position[c] = {*std::min_element(control.begin(), control.end())-error,
                    *std::max_element(control.begin(), control.end())+error};
            }
            anyInFront |= position[3].high > 0;
            if (position[3].low <= 0) { crossingEye = true; continue; }
            const auto x = position[0]/position[3], y = position[1]/position[3];
            const double cx=mapping.offsetX, cy=mapping.offsetY;
            const double sx=mapping.scaleX, sy=mapping.scaleY;
            result.include({cx+x.low*sx, cx+x.high*sx, cy-y.high*sy, cy-y.low*sy});
        }
        if (!anyInFront) { return {1, 0, 1, 0}; }

        return crossingEye ? parent : result.intersect(parent);
    }
};

void begin_visibility(uint8_t* rdram, uint64_t frame, uint32_t camera, uint32_t view,
    bool cut, Matrix lens, const Matrix& cameraView, const VisibilityViewport* screen = nullptr);
void reset_visibility();
void retire_visibility(uint32_t address, uint32_t size);
}
