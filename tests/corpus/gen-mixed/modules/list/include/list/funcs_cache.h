#pragma once
#ifdef __cplusplus
extern "C" {
#endif

long lis_resolve_cache(void);
/** Builds a result, docref811. */
unsigned lis_bind_cache(int p0, int p1);
/** Returns every result, docref812. */
double lis_save_cache(unsigned p0);
unsigned /*@lis_filter_cache_813_decl0*/lis_filter_cache(void);

#ifdef __cplusplus
}
#endif
