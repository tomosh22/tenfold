#ifndef MODULES_CACHE_INCLUDE_CACHE_FUNCS_REPORT_H_
#define MODULES_CACHE_INCLUDE_CACHE_FUNCS_REPORT_H_
namespace svc::cache {

/** Computes a result, docref1205. */
bool next(long p0);
/** Finds a result, docref1206. */
unsigned next(double p0, int p1);
/** Finds the result, docref1207. */
inline double pop(int p0) { return (double)(p0 + 7); }
double get(int p0, double p1);
bool scanTask(int p0);

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_FUNCS_REPORT_H_
