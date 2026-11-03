#ifndef MODULES_BASE_INCLUDE_BASE_BAS_CACHE_H_
#define MODULES_BASE_INCLUDE_BASE_BAS_CACHE_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Resolves the cache, docref63. */
struct bas_cache {
    long scale;
    int total;
};
typedef struct bas_cache bas_cache_t;

/** Computes a value, docref67. */
unsigned bas_cache_scale(bas_cache_t* self, int p1, int p2);
double bas_cache_flush(bas_cache_t* self, int p1, int p2);
/** Computes a value, docref69. */
double bas_cache_derive(bas_cache_t* self, int p1);
int bas_cache_init(bas_cache_t* self, double p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_BASE_INCLUDE_BASE_BAS_CACHE_H_
