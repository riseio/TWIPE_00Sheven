#pragma once

#include "native_render_matrix.hpp"
#include "native_clip_policy.hpp"
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
    size_t controlCount = 3;
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
        if (clip[0] == clip[1] && clip[1] == clip[2]) controlCount = 1;

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

        const std::array<std::array<double, 4>, 7> planes{{
                std::array<double, 4>{1, 0, 0, -left}, {-1, 0, 0, right},
                {0, 1, 0, -bottom}, {0, -1, 0, top}, {0, 0, 0, 1},
                {0, 0, 1, 1}, {0, 0, -1, 1}}};
        for (size_t p = 0; p < (native_depth_clipping ? 7 : 5); ++p) {
            const auto& plane = planes[p];
            double maximum = -std::numeric_limits<double>::infinity();
            for (size_t n = 0; n < controlCount; ++n) {
                const auto& control = clip[n];
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
        for (size_t n = 0; n < controlCount; ++n) {
            const auto& control = clip[n];
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
        if (parent.empty()) { return parent; }
        struct Point { double x, y; };

        std::array<Point, 9> points{};
        size_t count = 0;
        double marginX = 0, marginY = 0;
        bool crossingEye = false, anyInFront = false;
        unsigned outside = 15;
        for (const auto& vertex : vertices) {
            for (size_t n = 0; n < controlCount; ++n) {
                const auto& control = clip[n];
                std::array<double, 4> center{}, error{};
                for (size_t c : {0U, 1U, 3U}) {
                    double value = control[12+c], magnitude = 1 + std::abs(value);
                    for (size_t r = 0; r < 3; ++r) {
                        const double term = double(vertex[r])*64*control[r*4+c];
                        value += term; magnitude += std::abs(term);
                    }
                    error[c] = magnitude * 32 * std::numeric_limits<float>::epsilon();
                    center[c] = value;
                }
                const double left = mapping.offsetX-parent.left, right = parent.right-mapping.offsetX;
                const double top = mapping.offsetY-parent.top, bottom = parent.bottom-mapping.offsetY;
                const double x = center[0]*mapping.scaleX, y = -center[1]*mapping.scaleY, w = center[3];
                const double ex = error[0]*mapping.scaleX, ey = error[1]*mapping.scaleY, ew = error[3];
                unsigned code = 0;
                if (x+left*w+ex+std::abs(left)*ew < 0) code |= 1;
                if (-x+right*w+ex+std::abs(right)*ew < 0) code |= 2;
                if (y+top*w+ey+std::abs(top)*ew < 0) code |= 4;
                if (-y+bottom*w+ey+std::abs(bottom)*ew < 0) code |= 8;
                outside &= code;
                anyInFront |= w+ew > 0;
                if (w-ew <= 0) { crossingEye = true; continue; }
                const double px = x/w, py = y/w;

                marginX = std::max(marginX, (ex+std::abs(px)*ew)/(w-ew));
                marginY = std::max(marginY, (ey+std::abs(py)*ew)/(w-ew));
                points[count++] = {mapping.offsetX+px, mapping.offsetY+py};
            }
        }
        if (!anyInFront || outside) { return {1, 0, 1, 0}; }

        if (crossingEye) { return parent; }
        std::sort(points.begin(), points.begin()+count, [](Point a, Point b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        const auto turn = [](Point a, Point b, Point c) {
            return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
        };
        std::array<Point, 24> polygon{}, scratch{};
        size_t size = 0;
        for (size_t i = 0; i < count; ++i) {
            while (size >= 2 && turn(polygon[size-2], polygon[size-1], points[i]) <= 0) --size;
            polygon[size++] = points[i];
        }
        const size_t lower = size;
        for (size_t i = count-1; i-- > 0;) {
            while (size > lower && turn(polygon[size-2], polygon[size-1], points[i]) <= 0) --size;
            polygon[size++] = points[i];
        }
        if (size) --size;

        const VisibilityRect window{parent.left-marginX, parent.right+marginX,
            parent.top-marginY, parent.bottom+marginY};
        for (unsigned plane = 0; plane < 4 && size; ++plane) {
            const auto distance = [&](Point p) {
                switch (plane) {
                case 0: return p.x-window.left;
                case 1: return window.right-p.x;
                case 2: return p.y-window.top;
                default: return window.bottom-p.y;
                }
            };
            size_t written = 0;
            Point a = polygon[size-1];
            double da = distance(a);
            for (size_t i = 0; i < size; ++i) {
                const Point b = polygon[i];
                const double db = distance(b);
                if ((da < 0) != (db < 0)) {
                    const double t = da/(da-db);
                    scratch[written++] = {a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t};
                }
                if (db >= 0) scratch[written++] = b;
                a = b; da = db;
            }
            polygon.swap(scratch); size = written;
        }
        if (!size) { return {1, 0, 1, 0}; }
        VisibilityRect result{polygon[0].x, polygon[0].x, polygon[0].y, polygon[0].y};
        for (size_t i = 1; i < size; ++i) {
            result.left = std::min(result.left, polygon[i].x);
            result.right = std::max(result.right, polygon[i].x);
            result.top = std::min(result.top, polygon[i].y);
            result.bottom = std::max(result.bottom, polygon[i].y);
        }
        return VisibilityRect{result.left-marginX, result.right+marginX,
            result.top-marginY, result.bottom+marginY}.intersect(parent);
    }
};

void begin_visibility(uint8_t* rdram, uint64_t frame, uint32_t camera, uint32_t view,
    bool cut, Matrix lens, const Matrix& cameraView, const VisibilityViewport* screen = nullptr);
void reset_visibility();
void retire_visibility(uint32_t address, uint32_t size);
}
