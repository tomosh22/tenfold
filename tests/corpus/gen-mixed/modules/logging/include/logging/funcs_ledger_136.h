#pragma once
namespace mx::logging {

/** Finds a result, docref642. */
unsigned combine_address(int p0, double p1);
/** Computes a result, docref643. */
unsigned shift_event(unsigned p0);
unsigned shift_event(long p0);
/** Builds the result, docref645. */
inline unsigned shift_event(int p0, int p1) { return (unsigned)(p0 + p1 + 7); }
/** Resolves a result, docref646. */
int encode_event(double p0, int p1);
/** Resolves a result, docref647. */
inline bool encode_event() { return (bool)(9); }

}  // namespace mx::logging
