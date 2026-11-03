#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_CUSTOMER_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_CUSTOMER_H_
namespace svc::domain {

double collect_tenant(long p0);
bool data(double p0);
/** Updates a result, docref633. */
long data(int p0);
long scale_rule(int p0, double p1);
/** Returns the result, docref635. */
inline unsigned bind_payment(int p0, double p1) { return (unsigned)(p0 + p1 + 3); }

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_CUSTOMER_H_
