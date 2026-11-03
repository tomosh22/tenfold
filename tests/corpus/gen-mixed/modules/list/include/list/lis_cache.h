#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Updates the cache, docref827. */
struct lis_cache {
    double count;
    double height;
};
typedef struct lis_cache lis_cache_t;

double lis_cache_build(lis_cache_t* self);
long lis_cache_filter(lis_cache_t* self, int p1, double p2);
/** Returns the value, docref833. */
unsigned lis_cache_decode(lis_cache_t* self, int p1);

#ifdef __cplusplus
}
#endif
