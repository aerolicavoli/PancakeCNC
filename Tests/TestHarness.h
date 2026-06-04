#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#define EXPECT_TRUE(condition)                                                                    \
    do                                                                                            \
    {                                                                                             \
        if (!(condition))                                                                         \
        {                                                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << " expected true: " #condition << '\n';   \
            std::exit(EXIT_FAILURE);                                                              \
        }                                                                                         \
    } while (false)

#define EXPECT_FALSE(condition) EXPECT_TRUE(!(condition))

#define EXPECT_EQ(actual, expected)                                                               \
    do                                                                                            \
    {                                                                                             \
        const auto actual_value = (actual);                                                       \
        const auto expected_value = (expected);                                                    \
        if (!(actual_value == expected_value))                                                     \
        {                                                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << " expected " #actual " == " #expected    \
                      << " but got " << actual_value << " vs " << expected_value << '\n';        \
            std::exit(EXIT_FAILURE);                                                              \
        }                                                                                         \
    } while (false)

inline void ExpectNearlyEqual(float actual, float expected, float tolerance, const char *label)
{
    if (std::fabs(actual - expected) > tolerance)
    {
        std::cerr << label << " expected " << expected << " but got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

inline void PrintTestPassed(const std::string &test_name)
{
    std::cout << test_name << " passed\n";
}

#endif // TEST_HARNESS_H
