#pragma once

#include <cmath>

struct VectorAbstract {
    double x = 0;
    double y = 0;
};

class Vector : public VectorAbstract {
public:
    Vector(double x = 0, double y = 0) { this->x = x; this->y = y; }

    static bool isFinite(const VectorAbstract& v) { return std::isfinite(v.x) && std::isfinite(v.y); }

    static Vector fromPolar(double theta, double distance) {
        return Vector(distance * std::cos(theta), distance * std::sin(theta));
    }

    void set(const VectorAbstract& v) { x = v.x; y = v.y; }
    void add(const VectorAbstract& v) { x += v.x; y += v.y; }
    void subtract(const VectorAbstract& v) { x -= v.x; y -= v.y; }

    double distanceToSQ(const VectorAbstract& v) const {
        double dx = v.x - x, dy = v.y - y;
        return dx * dx + dy * dy;
    }

    double magnitude() const { return std::sqrt(x * x + y * y); }
    void setMagnitude(double m) {
        double dir = angle();
        x = std::cos(dir) * m;
        y = std::sin(dir) * m;
    }

    double angle() const { return std::atan2(y, x); }
    void setAngle(double a) {
        double m = magnitude();
        x = std::cos(a) * m;
        y = std::sin(a) * m;
    }
};
