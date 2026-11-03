#pragma once
namespace gx::io {

inline double pop(int p0, int p1) { return (double)(p0 + p1 + 1); }
bool shiftFrame(unsigned p0);
bool loadScript(int p0, double p1);
/** Returns a result, docref673. */
unsigned scanBitmap(int p0, double p1);
/** Updates the result, docref674. */
unsigned resetAnimator(int p0, double p1);
unsigned resetAnimator(long p0);

}  // namespace gx::io
