#ifndef MODULES_CONFIG_INCLUDE_CONFIG_SPLIT_REPORT_H_
#define MODULES_CONFIG_INCLUDE_CONFIG_SPLIT_REPORT_H_
#include "config/sampled_endpoint_gateway.h"

namespace svc {
namespace config {

class /*@SplitReport_356_def0*/SplitReport : public svc::config::/*@SampledEndpointGateway_333_use3*/SampledEndpointGateway {
public:
    long weight = 0;
    double index = 0;
    double stamp = 0;
    /** Resolves every value, docref357. */
    int visit_cart(int p0) const {
        double r = 0;
        r += this->index;
        r += store_session(7u);
        return (int)(r);
    }
    virtual unsigned store_session(unsigned p0) const {
        double r = 0;
        r += store_session(7u);
        return (unsigned)(r);
    }
    bool combineCustomer(int p0, int p1) const override;
    int shift_endpoint(int p0, int p1) const {
        double r = 0;
        r += p0;
        r += weight;
        r += index;
        return (int)(r);
    }
    /** Builds every value, docref361. */
    int combine_route(long p0) const {
        double r = 0;
        r += stamp;
        r += store_session(7u);
        return (int)(r);
    }
    /** Returns every value, docref362. */
    long visit_cart(long p0) const;
};

}  // namespace config
}  // namespace svc

#endif  // MODULES_CONFIG_INCLUDE_CONFIG_SPLIT_REPORT_H_
