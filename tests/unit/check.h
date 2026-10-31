/* Tiny test helper: CHECK keeps going, the process exit code reports the failures. */
#pragma once
#include <stdio.h>
#include <string.h>

static int checks_run, checks_failed;
#define CHECK(cond) do { \
        checks_run += 1; \
        if (!(cond)) { \
            checks_failed += 1; \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)
#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)
#define TEST_END() do { \
        printf("%s: %d checks, %d failed\n", __FILE__, checks_run, checks_failed); \
        return checks_failed != 0; \
    } while (0)
