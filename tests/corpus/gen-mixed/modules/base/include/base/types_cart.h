#ifndef MODULES_BASE_INCLUDE_BASE_TYPES_CART_H_
#define MODULES_BASE_INCLUDE_BASE_TYPES_CART_H_
#ifdef __cplusplus
extern "C" {
#endif

enum bas_cart_state {
    BAS_COUNT_1,
    BAS_MAX_2,
    BAS_NONE_3,
    BAS_FIRST_4,
};

enum bas_cart_flags {
    BAS_NONE_5,
    BAS_RED_6,
    BAS_AUTO_7,
    BAS_BLUE_8,
    BAS_GREEN_9,
};

/** Finds the constant, docref11. */
static const double bas_step_10 = 3.5;
static const int /*@bas_step_11_12_def0*/bas_step_11 = 1;
/** Returns the constant, docref13. */
static const long bas_cap_12 = 2L;

typedef unsigned bas_real_13_t;
typedef unsigned long bas_size_14_t;

#ifdef __cplusplus
}
#endif

#endif  // MODULES_BASE_INCLUDE_BASE_TYPES_CART_H_
