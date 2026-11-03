#pragma once
namespace gx::memory {

double rotateBounds(unsigned p0);
inline bool fetchMesh(int p0, int p1) { return (bool)(p0 + p1 + 1); }
unsigned selectCurve(int p0, int p1);
/** Builds the result, docref357. */
unsigned selectCurve(int p0, double p1);

}  // namespace gx::memory
