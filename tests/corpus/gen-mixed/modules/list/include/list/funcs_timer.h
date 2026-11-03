#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Builds a result, docref908. */
double lis_store_timer(int p0, int p1);
static inline double lis_encode_timer(void) { return (double)(2); }
/** Computes every result, docref910. */
static inline long lis_visit_timer(long p0) { return (long)(p0 + 5); }

#ifdef __cplusplus
}
#endif
