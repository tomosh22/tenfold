#ifndef MODULES_LOGGING_INCLUDE_LOGGING_REGISTRY_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_REGISTRY_H_
#include "mstd.h"

namespace svc {
namespace logging {

/** Finds every registry, docref413. */
class /*@Registry_413_def0*/Registry {
public:
    struct Entry { int val = 0; };
    long mass = 0;
    mstd::vector<int> items74;
    /** Builds a value, docref414. */
    double combine_gateway(int p0) const;
    long save_notice(int p0);
    int name(int p0) const;
    /** Updates the value, docref417. */
    double combine_gateway(long p0) const;
    int data(unsigned p0) {
        double r = 0;
        r += p0;
        r += mass;
        r += combine_gateway(2L);
        return (int)(r);
    }
    /** Updates a value, docref419. */
    virtual long combine_invoice(double p0, int p1) const;
};

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_REGISTRY_H_
