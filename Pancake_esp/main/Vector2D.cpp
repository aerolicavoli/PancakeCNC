#include "Vector2D.h"

// Copy constructor
Vector2D::Vector2D(const Vector2D &other) : x(other.x), y(other.y) {}

Vector2D Vector2D::operator+(const Vector2D &other) const
{
    return Vector2D(x + other.x, y + other.y);
}

Vector2D Vector2D::operator+(float scalar) const { return Vector2D(x + scalar, y + scalar); }

Vector2D Vector2D::operator-(const Vector2D &other) const
{
    return Vector2D(x - other.x, y - other.y);
}

Vector2D Vector2D::operator-(float scalar) const { return Vector2D(x - scalar, y - scalar); }

Vector2D Vector2D::operator*(float scalar) const { return Vector2D(x * scalar, y * scalar); }

Vector2D Vector2D::operator/(float scalar) const { return Vector2D(x / scalar, y / scalar); }

Vector2D Vector2D::operator=(const Vector2D &other)
{
    if (this != &other) // Check for self-assignment
    {
        x = other.x;
        y = other.y;
    }
    return *this;
}

float dot(const Vector2D &lhs, const Vector2D &rhs) { return (lhs.x * rhs.x + lhs.y * rhs.y); }

float Vector2D::magnitude() const { return sqrtf(dot(*this, *this)); }
