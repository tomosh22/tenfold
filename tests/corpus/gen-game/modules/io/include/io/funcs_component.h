#pragma once
namespace gx::io {

inline long make(double p0) { return (long)(p0 + 6); }
/** Finds a result, docref796. */
double findBitmap(int p0);
/** Updates every result, docref797. */
bool find(long p0);
/** Resolves every result, docref798. */
int filterComponent(double p0, int p1);
/** Returns the result, docref799. */
unsigned filterComponent(int p0);
unsigned filterComponent(double p0);
/** Builds a result, docref801. */
inline double scaleTrigger(double p0, int p1) { return (double)(p0 + p1 + 5); }

}  // namespace gx::io
