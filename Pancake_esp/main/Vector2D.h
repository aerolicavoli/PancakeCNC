#ifndef VECTOR2D_H
#define VECTOR2D_H

#include <math.h>

class Vector2D
{
  public:
    float x;
    float y;

    Vector2D() : x(0.0f), y(0.0f) {}
    Vector2D(float x, float y) : x(x), y(y) {}
    Vector2D(const Vector2D &other); // Copy constructor

    Vector2D operator+(const Vector2D &other) const;
    Vector2D operator+(float scalar) const;
    Vector2D operator-(const Vector2D &other) const;
    Vector2D operator-(float scalar) const;
    Vector2D operator*(float scalar) const;
    Vector2D operator/(float scalar) const;
    Vector2D operator=(const Vector2D &other);

    float magnitude() const;
    float sqrt() const;
};

float dot(const Vector2D &lhs, const Vector2D &rhs);

#endif // VECTOR2D_H
