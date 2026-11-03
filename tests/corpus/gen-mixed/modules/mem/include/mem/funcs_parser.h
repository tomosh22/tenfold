#pragma once
#ifdef __cplusplus
extern "C" {
#endif

int mem_find_parser(int p0, int p1);
/** Returns a result, docref383. */
double mem_decode_parser(unsigned p0);
double mem_probe_parser(double p0, int p1);
/** Computes every result, docref385. */
unsigned mem_mark_parser(void);
/** Builds the result, docref386. */
long mem_update_parser(void);

#ifdef __cplusplus
}
#endif
