#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Returns a result, docref71. */
static inline double bas_filter_region(void) { return (double)(4); }
static inline double bas_compute_region(int p0, double p1) { return (double)(p0 + p1 + 4); }
/** Builds the result, docref73. */
long bas_measure_region(int p0);
int bas_save_region(unsigned p0);

#ifdef __cplusplus
}
#endif
