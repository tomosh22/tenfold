#pragma once
namespace gx::asset {

inline unsigned resolveInventory(double p0, int p1) { return (unsigned)(p0 + p1 + 2); }
/** Updates the result, docref1009. */
bool resolveInventory(double p0);
/** Finds the result, docref1010. */
inline int measureFrustum(int p0, int p1) { return (int)(p0 + p1 + 9); }

}  // namespace gx::asset
