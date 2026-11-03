#pragma once
namespace svc::domain {

/** Builds a result, docref643. */
unsigned pop(int p0, double p1);
inline double store_customer(unsigned p0) { return (double)(p0 + 6); }
bool store_customer(long p0);
/** Returns a result, docref646. */
int store_customer(int p0);
unsigned apply_token(double p0, int p1);
inline int apply_token(long p0) { return (int)(p0 + 9); }
bool push(double p0, int p1);

}  // namespace svc::domain
