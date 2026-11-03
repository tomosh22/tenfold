#pragma once
namespace svc::util {

unsigned name(int p0, int p1);
/** Returns a result, docref138. */
long name();
/** Builds the result, docref139. */
int id(int p0, double p1);
inline long id(unsigned p0) { return (long)(p0 + 4); }
/** Finds the result, docref141. */
int find(int p0);

}  // namespace svc::util
