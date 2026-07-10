#include "test_support.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Test_Case {
    const char *name;
    Test_Function function;
    struct Test_Case *next;
} Test_Case;

static Test_Case *test_cases;
static Test_Case **test_cases_tail = &test_cases;
static size_t current_failures;

void test_register(const char *name, Test_Function function)
{
    Test_Case *test_case = malloc(sizeof(*test_case));
    if (test_case == NULL) {
        fprintf(stderr, "fatal: could not register test %s\n", name);
        abort();
    }
    *test_case = (Test_Case) {.name = name, .function = function};
    *test_cases_tail = test_case;
    test_cases_tail = &test_case->next;
}

void test_fail(const char *file, int line, const char *expression, const char *format, ...)
{
    current_failures += 1;
    fprintf(stderr, "\n    %s:%d: ", file, line);
    if (expression != NULL) fprintf(stderr, "expectation `%s` failed", expression);
    if (format != NULL) {
        if (expression != NULL) fputs(": ", stderr);
        va_list arguments;
        va_start(arguments, format);
        vfprintf(stderr, format, arguments);
        va_end(arguments);
    }
}

int test_run_all(void)
{
    size_t passed = 0;
    size_t failed = 0;
    size_t total = 0;
    for (Test_Case *test_case = test_cases; test_case != NULL; test_case = test_case->next) {
        total += 1;
        current_failures = 0;
        fprintf(stderr, "[TEST] %s", test_case->name);
        test_case->function();
        if (current_failures == 0) {
            passed += 1;
            fputs(" ok\n", stderr);
        } else {
            failed += 1;
            fprintf(stderr, "\n       FAILED (%zu expectation%s)\n", current_failures,
                    current_failures == 1 ? "" : "s");
        }
    }

    fprintf(stderr, "\n%zu/%zu tests passed", passed, total);
    if (failed != 0) fprintf(stderr, ", %zu failed", failed);
    fputc('\n', stderr);

    while (test_cases != NULL) {
        Test_Case *next = test_cases->next;
        free(test_cases);
        test_cases = next;
    }
    test_cases_tail = &test_cases;
    return failed == 0 ? 0 : 1;
}
