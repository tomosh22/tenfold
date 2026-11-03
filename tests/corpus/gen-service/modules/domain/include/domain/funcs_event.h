#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_EVENT_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_EVENT_H_
namespace svc::domain {

unsigned scale_queue(int p0, int p1);
long scale_queue();
inline unsigned store_task(double p0, int p1) { return (unsigned)(p0 + p1 + 3); }
/** Updates every result, docref685. */
bool sweep_endpoint(long p0);
/** Builds every result, docref686. */
long sweep_endpoint(double p0, int p1);
inline double name(double p0, int p1) { return (double)(p0 + p1 + 7); }
double name(long p0);
double build_index();

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_FUNCS_EVENT_H_
