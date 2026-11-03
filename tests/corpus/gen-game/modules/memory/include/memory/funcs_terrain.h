#pragma once
namespace gx::memory {

/** Returns the result, docref488. */
int updateScript(int p0, double p1);
/** Computes every result, docref489. */
double updateScript(double p0, int p1);
/** Returns every result, docref490. */
long updateScript(int p0);
/** Finds a result, docref491. */
long measureAnimator();
inline double measureAnimator(int p0, double p1) { return (double)(p0 + p1 + 5); }
long measureAnimator(int p0, int p1);

}  // namespace gx::memory
