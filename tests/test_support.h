#ifndef MUSIALIZER_TEST_SUPPORT_H
#define MUSIALIZER_TEST_SUPPORT_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*Test_Function)(void);

void test_register(const char *name, Test_Function function);
void test_fail(const char *file, int line, const char *expression, const char *format, ...);
int test_run_all(void);

#if defined(__GNUC__) || defined(__clang__)
#define TEST(name)                                                               \
    static void test_##name(void);                                                \
    static void register_##name(void) __attribute__((constructor));               \
    static void register_##name(void) { test_register(#name, test_##name); }       \
    static void test_##name(void)
#else
#error "The automatic test registry currently requires GCC or Clang"
#endif

#define TEST_FAIL(...) test_fail(__FILE__, __LINE__, NULL, __VA_ARGS__)

#define EXPECT_TRUE(expression)                                                   \
    do {                                                                          \
        if (!(expression)) test_fail(__FILE__, __LINE__, #expression, NULL);       \
    } while (0)

#define EXPECT_FALSE(expression) EXPECT_TRUE(!(expression))

#define REQUIRE_TRUE(expression)                                                  \
    do {                                                                          \
        if (!(expression)) {                                                       \
            test_fail(__FILE__, __LINE__, #expression, NULL);                     \
            return;                                                               \
        }                                                                         \
    } while (0)

#define EXPECT_EQ_SIZE(actual, expected)                                          \
    do {                                                                          \
        size_t test_actual_ = (size_t) (actual);                                  \
        size_t test_expected_ = (size_t) (expected);                              \
        if (test_actual_ != test_expected_) {                                     \
            test_fail(__FILE__, __LINE__, #actual " == " #expected,              \
                      "actual=%zu expected=%zu", test_actual_, test_expected_);   \
        }                                                                         \
    } while (0)

#define EXPECT_EQ_U64(actual, expected)                                           \
    do {                                                                          \
        uint64_t test_actual_ = (uint64_t) (actual);                              \
        uint64_t test_expected_ = (uint64_t) (expected);                          \
        if (test_actual_ != test_expected_) {                                     \
            test_fail(__FILE__, __LINE__, #actual " == " #expected,              \
                      "actual=%llu expected=%llu",                               \
                      (unsigned long long) test_actual_,                           \
                      (unsigned long long) test_expected_);                        \
        }                                                                         \
    } while (0)

#define EXPECT_NEAR(actual, expected, tolerance)                                  \
    do {                                                                          \
        double test_actual_ = (double) (actual);                                  \
        double test_expected_ = (double) (expected);                              \
        double test_tolerance_ = (double) (tolerance);                            \
        if (!isfinite(test_actual_) ||                                             \
            fabs(test_actual_ - test_expected_) > test_tolerance_) {              \
            test_fail(__FILE__, __LINE__, #actual " ~= " #expected,              \
                      "actual=%.9g expected=%.9g tolerance=%.9g",                \
                      test_actual_, test_expected_, test_tolerance_);              \
        }                                                                         \
    } while (0)

#endif
