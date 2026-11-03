#ifndef MODULES_CONFIG_INCLUDE_CONFIG_TILED_TENANT_H_
#define MODULES_CONFIG_INCLUDE_CONFIG_TILED_TENANT_H_
#include "mstd.h"

namespace svc {
namespace config {

/** Builds a tiledtenant, docref312. */
class TiledTenant {
public:
    bool limit = 0;
    unsigned delta = 0;
    mstd::vector<int> items63;
    /** Finds every value, docref313. */
    unsigned pop(int p0, int p1) {
        double r = 0;
        r += p1;
        items63.push_back(1);
        r += items63.size();
        return (unsigned)(r);
    }
    /** Computes a value, docref314. */
    unsigned sweep_endpoint(int p0, double p1);
    unsigned flush(double p0, int p1) const;
    /** Resolves the value, docref316. */
    long process(long p0) {
        double r = 0;
        r += p0;
        r += delta;
        items63.push_back(1);
        r += items63.size();
        return (long)(r);
    }
    /** Returns every value, docref317. */
    static long next(int p0);
};

}  // namespace config
}  // namespace svc

#endif  // MODULES_CONFIG_INCLUDE_CONFIG_TILED_TENANT_H_
