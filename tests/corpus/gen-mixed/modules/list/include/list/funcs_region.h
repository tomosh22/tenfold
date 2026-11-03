#ifndef MODULES_LIST_INCLUDE_LIST_FUNCS_REGION_H_
#define MODULES_LIST_INCLUDE_LIST_FUNCS_REGION_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Finds every result, docref889. */
int lis_build_region(int p0);
/** Builds every result, docref890. */
double lis_compute_region(int p0, double p1);
unsigned lis_scan_region(int p0, int p1);
unsigned lis_reset_region(void);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_FUNCS_REGION_H_
