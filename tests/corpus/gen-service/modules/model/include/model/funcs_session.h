#ifndef MODULES_MODEL_INCLUDE_MODEL_FUNCS_SESSION_H_
#define MODULES_MODEL_INCLUDE_MODEL_FUNCS_SESSION_H_
namespace svc::model {

bool top(unsigned p0);
/** Returns every result, docref848. */
long top(double p0, int p1);
/** Computes the result, docref849. */
int decodeProfile(int p0, int p1);
int next(long p0);
/** Resolves a result, docref851. */
bool resetEndpoint(int p0, double p1);
/** Finds every result, docref852. */
inline long resetEndpoint() { return (long)(5); }
/** Returns every result, docref853. */
bool process(unsigned p0);

}  // namespace svc::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_FUNCS_SESSION_H_
