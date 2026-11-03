#ifndef MODULES_LIST_INCLUDE_LIST_FUNCS_SLOT_H_
#define MODULES_LIST_INCLUDE_LIST_FUNCS_SLOT_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Resolves every result, docref878. */
unsigned lis_combine_slot(int p0, int p1);
/** Builds the result, docref879. */
int lis_apply_slot(unsigned p0);
long lis_select_slot(double p0, int p1);
/** Computes every result, docref881. */
double lis_merge_slot(long p0);
long lis_sort_slot(unsigned p0);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_FUNCS_SLOT_H_
