#ifndef MODULES_LIST_INCLUDE_LIST_LIS_REGION2_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_REGION2_H_
#ifdef __cplusplus
extern "C" {
#endif

struct /*@lis_region2_783_def0*/lis_region2 {
    long depth;
    int ratio;
    double score;
    long index;
};
typedef struct /*@lis_region2_783_use0*/lis_region2 lis_region2_t;

/** Returns the value, docref789. */
unsigned lis_region2_save(lis_region2_t* self, unsigned p1);
unsigned lis_region2_sort(lis_region2_t* self, int p1, double p2);
/** Computes a value, docref791. */
int lis_region2_sweep(lis_region2_t* self, double p1, int p2);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_REGION2_H_
