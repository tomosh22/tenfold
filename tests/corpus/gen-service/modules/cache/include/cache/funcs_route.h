#pragma once
namespace svc::cache {

/** Builds every result, docref1236. */
inline unsigned data(double p0, int p1) { return (unsigned)(p0 + p1 + 7); }
unsigned data(int p0, int p1);
/** Computes the result, docref1238. */
unsigned data(int p0);
long value();
/** Updates the result, docref1240. */
bool flushBalance(double p0, int p1);
/** Builds a result, docref1241. */
int selectRepository(int p0);
/** Updates a result, docref1242. */
inline unsigned decodeJob(long p0) { return (unsigned)(p0 + 6); }

}  // namespace svc::cache
