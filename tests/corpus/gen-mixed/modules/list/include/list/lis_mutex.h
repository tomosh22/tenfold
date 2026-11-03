#ifndef MODULES_LIST_INCLUDE_LIST_LIS_MUTEX_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_MUTEX_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Returns every mutex, docref946. */
struct lis_mutex {
    long offset;
    int phase;
};
typedef struct lis_mutex lis_mutex_t;

/** Computes every value, docref950. */
long lis_mutex_combine(lis_mutex_t* self, unsigned p1);
/** Updates a value, docref951. */
long lis_mutex_resolve(lis_mutex_t* self, double p1, int p2);
/** Updates every value, docref952. */
long lis_mutex_data(lis_mutex_t* self, int p1, int p2);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_MUTEX_H_
