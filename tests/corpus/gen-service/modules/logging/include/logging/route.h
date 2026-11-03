#ifndef MODULES_LOGGING_INCLUDE_LOGGING_ROUTE_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_ROUTE_H_
namespace svc {
namespace logging {

/** Finds a route, docref395. */
class Route {
public:
    virtual ~Route() {}
    /** Finds a value, docref396. */
    virtual unsigned push(int p0, int p1) const = 0;
    virtual long set(int p0) = 0;
    virtual double visit_tenant(int p0, double p1) = 0;
    virtual bool collect_tariff(unsigned p0) = 0;
};

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_ROUTE_H_
