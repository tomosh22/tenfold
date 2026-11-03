#pragma once
#ifdef __cplusplus
extern "C" {
#endif

enum lis_token_flags {
    LIS_NONE_180,
    LIS_LAST_181,
    LIS_BLUE_182,
    LIS_GREEN_183,
    LIS_MAX_184,
};

/** Builds a enumeration, docref840. */
enum lis_token_state {
    LIS_AUTO_185,
    LIS_FIRST_186,
    LIS_DEFAULT_187,
};

/** Builds the constant, docref844. */
static const int lis_step_188 = 1;
/** Returns the constant, docref845. */
static const unsigned lis_limit_189 = 7u;
/** Updates the constant, docref846. */
static const int lis_base_190 = 1;

typedef double lis_id_191_t;

#ifdef __cplusplus
}
#endif
