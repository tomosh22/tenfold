#ifndef MODULES_BASE_INCLUDE_BASE_BAS_CACHE2_H_
#define MODULES_BASE_INCLUDE_BASE_BAS_CACHE2_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Resolves the cache, docref102. */
struct bas_cache2 {
    unsigned ratio;
    double depth;
    unsigned speed;
    unsigned count;
};
typedef struct bas_cache2 bas_cache2_t;

/** Computes the value, docref108. */
long bas_cache2_mark(bas_cache2_t* self);
/** Finds the value, docref109. */
unsigned bas_cache2_derive(bas_cache2_t* self, unsigned p1);
/** Finds the value, docref110. */
int bas_cache2_save(bas_cache2_t* self, unsigned p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_BASE_INCLUDE_BASE_BAS_CACHE2_H_
