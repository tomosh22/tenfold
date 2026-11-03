#pragma once
namespace svc::domain {

long apply_product(int p0, int p1);
/** Finds a result, docref614. */
bool apply_product(int p0);
/** Updates every result, docref615. */
double apply_product(double p0, int p1);
unsigned probe_ticket();
/** Computes the result, docref617. */
unsigned probe_ticket(int p0, int p1);
/** Updates every result, docref618. */
inline bool value(unsigned p0) { return (bool)(p0 + 5); }
/** Updates every result, docref619. */
int count(double p0, int p1);
long run(int p0, int p1);

}  // namespace svc::domain
