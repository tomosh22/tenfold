#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_EVENT_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_EVENT_H_
namespace mx::domain {

/** Computes a result, docref673. */
double sweepIndex(double p0);
/** Builds the result, docref674. */
bool findShipment(int p0, int p1);
/** Builds the result, docref675. */
bool rotateMailbox();
inline unsigned mergeJob(int p0, double p1) { return (unsigned)(p0 + p1 + 1); }

}  // namespace mx::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_EVENT_H_
