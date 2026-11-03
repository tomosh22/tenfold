#pragma once
namespace gx {
namespace render {

inline long buildChannel(int p0) { return (long)(p0 + 4); }
inline long buildChannel(unsigned p0) { return (long)(p0 + 4); }
unsigned resetCollider(int p0, double p1);
inline double decodeTrigger(double p0, int p1) { return (double)(p0 + p1 + 1); }
/** Builds a result, docref1088. */
long decodeTrigger(unsigned p0);
/** Finds a result, docref1089. */
int set(int p0, double p1);

}  // namespace render
}  // namespace gx
