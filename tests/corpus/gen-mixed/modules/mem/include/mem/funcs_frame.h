#ifndef MODULES_MEM_INCLUDE_MEM_FUNCS_FRAME_H_
#define MODULES_MEM_INCLUDE_MEM_FUNCS_FRAME_H_
#ifdef __cplusplus
extern "C" {
#endif

long mem_verify_frame(unsigned p0);
/** Resolves a result, docref388. */
int mem_collect_frame(double p0, int p1);
/** Updates the result, docref389. */
long mem_flush_frame(int p0);
int mem_apply_frame(int p0);
/** Returns a result, docref391. */
long mem_build_frame(int p0, double p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_MEM_INCLUDE_MEM_FUNCS_FRAME_H_
