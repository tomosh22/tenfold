#ifndef MODULES_MODEL_INCLUDE_MODEL_FUNCS_ENDPOINT_H_
#define MODULES_MODEL_INCLUDE_MODEL_FUNCS_ENDPOINT_H_
namespace mx::model {

/** Resolves every result, docref984. */
double top(int p0, int p1);
/** Resolves a result, docref985. */
bool id(double p0, int p1);
/** Builds every result, docref986. */
inline unsigned id(unsigned p0) { return (unsigned)(p0 + 3); }
/** Returns every result, docref987. */
unsigned filterQuota(int p0, double p1);
/** Computes every result, docref988. */
double collectValidator(long p0);

}  // namespace mx::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_FUNCS_ENDPOINT_H_
