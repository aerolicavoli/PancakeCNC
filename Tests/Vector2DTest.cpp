#include <cstdlib>

#include "TestHarness.h"
#include "Vector2D.h"

int main()
{
    const Vector2D a(3.0f, 4.0f);
    const Vector2D b(-1.5f, 2.0f);

    Vector2D sum = a + b;
    ExpectNearlyEqual(sum.x, 1.5f, 1.0e-6f, "vector sum x");
    ExpectNearlyEqual(sum.y, 6.0f, 1.0e-6f, "vector sum y");

    Vector2D difference = a - b;
    ExpectNearlyEqual(difference.x, 4.5f, 1.0e-6f, "vector difference x");
    ExpectNearlyEqual(difference.y, 2.0f, 1.0e-6f, "vector difference y");

    Vector2D scaled = a * 2.0f;
    ExpectNearlyEqual(scaled.x, 6.0f, 1.0e-6f, "scaled vector x");
    ExpectNearlyEqual(scaled.y, 8.0f, 1.0e-6f, "scaled vector y");

    Vector2D divided = a / 2.0f;
    ExpectNearlyEqual(divided.x, 1.5f, 1.0e-6f, "divided vector x");
    ExpectNearlyEqual(divided.y, 2.0f, 1.0e-6f, "divided vector y");

    EXPECT_EQ(dot(a, b), 3.0f * -1.5f + 4.0f * 2.0f);
    ExpectNearlyEqual(a.magnitude(), 5.0f, 1.0e-6f, "vector magnitude");

    Vector2D assigned;
    assigned = a;
    ExpectNearlyEqual(assigned.x, a.x, 1.0e-6f, "assigned vector x");
    ExpectNearlyEqual(assigned.y, a.y, 1.0e-6f, "assigned vector y");

    PrintTestPassed("Vector2D unit test");
    return EXIT_SUCCESS;
}
