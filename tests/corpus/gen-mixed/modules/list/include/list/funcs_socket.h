#pragma once
#ifdef __cplusplus
extern "C" {
#endif

unsigned lis_sweep_socket(unsigned p0);
/** Resolves every result, docref884. */
int lis_flush_socket(unsigned p0);
/** Resolves every result, docref885. */
int lis_store_socket(long p0);
static inline unsigned lis_fetch_socket(int p0, double p1) { return (unsigned)(p0 + p1 + 9); }
/** Returns every result, docref887. */
long /*@lis_apply_socket_887_decl0*/lis_apply_socket(double p0, int p1);

#ifdef __cplusplus
}
#endif
