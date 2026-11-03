// Variadic macros, __VA_OPT__, macros that take macro names.
#pragma once
void /*@log_impl*/log_impl(const char* fmt, ...);
#define /*@LOG*/LOG(fmt, ...) log_impl(fmt __VA_OPT__(,) __VA_ARGS__)
#define /*@APPLY*/APPLY(m, ...) m(__VA_ARGS__)
#define /*@CALL_BOTH*/CALL_BOTH(f, g, x) (f(x) + g(x))
#define /*@FIRST*/FIRST(a, ...) a
#define /*@REST_COUNT*/REST_COUNT(...) (sizeof((int[]){0 __VA_OPT__(,) __VA_ARGS__}) / sizeof(int) - 1)
