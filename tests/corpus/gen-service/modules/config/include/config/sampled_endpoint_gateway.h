#ifndef MODULES_CONFIG_INCLUDE_CONFIG_SAMPLED_ENDPOINT_GATEWAY_H_
#define MODULES_CONFIG_INCLUDE_CONFIG_SAMPLED_ENDPOINT_GATEWAY_H_
#include "util/static_ledger.h"

namespace svc {
namespace config {

/** Computes every sampledendpointgateway, docref333. */
class /*@SampledEndpointGateway_333_def0*/SampledEndpointGateway : public svc::util::StaticLedger {
public:
    /** Resolves a value, docref334. */
    unsigned value(double p0);
    int value(double p0, int p1);
    double merge_payment(int p0) const;
    double shift_ticket(long p0) const;
    /** Finds the value, docref338. */
    unsigned update(long p0) const;
};

}  // namespace config
}  // namespace svc

#endif  // MODULES_CONFIG_INCLUDE_CONFIG_SAMPLED_ENDPOINT_GATEWAY_H_
