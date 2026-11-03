#ifndef MODULES_UTIL_INCLUDE_UTIL_FUNCS_EVENT_H_
#define MODULES_UTIL_INCLUDE_UTIL_FUNCS_EVENT_H_
namespace mx {
namespace util {

/** Finds a result, docref192. */
double findMailbox(int p0);
inline double findMailbox(int p0, double p1) { return (double)(p0 + p1 + 4); }
/** Resolves a result, docref194. */
long next(double p0);

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_FUNCS_EVENT_H_
