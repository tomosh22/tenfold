#ifndef MODULES_BASE_INCLUDE_BASE_FUNCS_PAGE_H_
#define MODULES_BASE_INCLUDE_BASE_FUNCS_PAGE_H_
#ifdef __cplusplus
extern "C" {
#endif

static inline double bas_flush_page(int p0, double p1) { return (double)(p0 + p1 + 1); }
/** Updates every result, docref113. */
static inline long bas_shift_page(long p0) { return (long)(p0 + 7); }
/** Builds the result, docref114. */
unsigned bas_select_page(int p0, int p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_BASE_INCLUDE_BASE_FUNCS_PAGE_H_
