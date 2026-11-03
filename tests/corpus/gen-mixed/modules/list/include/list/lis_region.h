#ifndef MODULES_LIST_INCLUDE_LIST_LIS_REGION_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_REGION_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Returns the region, docref735. */
struct lis_region {
    int depth;
};
typedef struct lis_region lis_region_t;

double lis_region_store(lis_region_t* self, long p1);
/** Finds a value, docref739. */
unsigned lis_region_size(lis_region_t* self, double p1, int p2);
/** Finds a value, docref740. */
long lis_region_add(lis_region_t* self, long p1);
unsigned lis_region_visit(lis_region_t* self, int p1);
unsigned lis_region_count(lis_region_t* self);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_REGION_H_
