#pragma once

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "data/bar.h"

inline int g_tests_run = 0;
inline int g_tests_passed = 0;
inline int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static void run_##name() { \
        ++g_tests_run; \
        printf("  %-50s ", #name); \
        test_##name(); \
        ++g_tests_passed; \
        printf("PASS\n"); \
    } \
    static void test_##name()

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++g_tests_failed; --g_tests_run; return; \
    }} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); if (_a != _b) { \
        printf("FAIL\n    %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
        ++g_tests_failed; --g_tests_run; return; \
    }} while(0)

#define ASSERT_NEAR(a, b, tol) \
    do { double _a = (a); double _b = (b); if (std::abs(_a - _b) > (tol)) { \
        printf("FAIL\n    %s:%d: |%s - %s| = |%.10f - %.10f| = %.10f > %s\n", \
            __FILE__, __LINE__, #a, #b, _a, _b, std::abs(_a - _b), #tol); \
        ++g_tests_failed; --g_tests_run; return; \
    }} while(0)

#define ASSERT_GT(a, b) \
    do { auto _a = (a); auto _b = (b); if (!(_a > _b)) { \
        printf("FAIL\n    %s:%d: %s > %s\n", __FILE__, __LINE__, #a, #b); \
        ++g_tests_failed; --g_tests_run; return; \
    }} while(0)

#define ASSERT_THROWS(expr) \
    do { bool caught = false; try { expr; } catch (...) { caught = true; } \
    if (!caught) { \
        printf("FAIL\n    %s:%d: expected exception from %s\n", __FILE__, __LINE__, #expr); \
        ++g_tests_failed; --g_tests_run; return; \
    }} while(0)

#define RUN_TEST(name) run_##name()

#define TEST_SUMMARY() \
    printf("\n  %d/%d passed", g_tests_passed, g_tests_passed + g_tests_failed); \
    if (g_tests_failed) printf(", %d FAILED", g_tests_failed); \
    printf("\n")

inline qe::Bar make_bar(int64_t ts, double o, double h, double l, double c,
                                double vol = 100.0) {
    return qe::Bar{ts, o, h, l, c, vol, vol * c};
}