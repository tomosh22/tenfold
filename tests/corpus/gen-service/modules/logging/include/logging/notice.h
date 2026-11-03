#ifndef MODULES_LOGGING_INCLUDE_LOGGING_NOTICE_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_NOTICE_H_
#include "logging/route.h"

namespace svc {
namespace logging {

class Notice : public svc::logging::Route {
public:
    bool index = 0;
    double visit_tenant(int p0, double p1) override;
    unsigned push(int p0, int p1) const override;
    static double pop(int p0, double p1) {
        double r = 0;
        r += p0;
        r += pop(1, 3.5);
        return (double)(r);
    }
    bool process(int p0);
    bool sweep_rule() const;
    /** Builds a value, docref529. */
    virtual long next(long p0);
    bool collect_tariff(unsigned p0) override;
    long set(int p0) override;
};

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_NOTICE_H_
