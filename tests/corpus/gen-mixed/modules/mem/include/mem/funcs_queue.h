#pragma once
#ifdef __cplusplus
extern "C" {
#endif

long mem_derive_queue(int p0, double p1);
/** Builds the result, docref405. */
static inline long mem_combine_queue(unsigned p0) { return (long)(p0 + 5); }
unsigned mem_count_queue(double p0, int p1);
/** Returns the result, docref407. */
int mem_store_queue(void);
unsigned mem_store_queue2(unsigned p0);

#ifdef __cplusplus
}
#endif
