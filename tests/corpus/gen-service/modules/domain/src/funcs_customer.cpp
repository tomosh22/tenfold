#include "domain/funcs_customer.h"
#include "domain/nested_endpoint.h"
#include "logging/types_mapper.h"
#include "logging/frozen_endpoint.h"
#include "domain/types_validator.h"
#include "util/types_profile.h"
#include "domain/funcs_index.h"
#include "logging/info.h"
#include "util/binned_gateway_job.h"
#include "util/profile.h"
#include "logging/macros_account.h"
#include "util/buffer.h"

namespace svc::domain {

double collect_tenant(long p0) {
    double r = 0;
    r += p0;
    svc::domain::NestedEndpoint o1;
    r += o1.combineCustomer(1, 1);
    r += o1./*@scan_token_624_use5*/scan_token(3.5, 1);
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += static_cast<int>(svc::logging::/*@Handle_374_use1*/Handle::Count);
    return (double)(r);
}

bool data(double p0) {
    double r = 0;
    svc::logging::FrozenEndpoint d1;
    svc::logging::FrozenEndpoint& b2 = d1;
    r += b2.sweep_quota(2L);
    r += svc::domain::kStep112;
    return (bool)(r);
}

long data(int p0) {
    double r = 0;
    r += p0;
    r += svc::util::kScale1;
    r += flush(1);
    svc::logging::Info o1;
    r += o1.phase;
    svc::util::BinnedGatewayJob o2;
    r += o2.mergeValidator(1);
    r += o2.combineCustomer(1, 1);
    r += svc::util::Profile::shared_scale;
    r += LOGGING_ENDPOINT_82;
    return (long)(r);
}

long scale_rule(int p0, double p1) {
    double r = 0;
    r += p0;
    svc::util::Buffer::/*@shared_ratio_33_use3*/shared_ratio++;
    svc::domain::NestedEndpoint o1;
    r += o1.build_endpoint();
    svc::domain::NestedEndpoint o2;
    r += o2./*@scan_token_624_use6*/scan_token(3.5, 1);
    return (long)(r);
}


}  // namespace svc::domain
