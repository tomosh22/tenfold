#include "logging/funcs_mapper.h"
#include "logging/types_mapper.h"
#include "logging/registry.h"
#include "logging/funcs_policy.h"
#include "util/types_session.h"
#include "util/types_profile_13.h"
#include "util/profile.h"
#include "util/static_ledger.h"
#include "util/provider.h"
#include "logging/types_balance.h"
#include "util/buffer.h"
#include "logging/funcs_balance.h"

namespace svc {
namespace logging {

unsigned bind_tenant(int p0) {
    double r = 0;
    r += p0;
    r += svc::logging::kLimit68;
    r += static_cast<int>(svc::logging::Helper::Default);
    svc::logging::/*@Registry_413_use10*/Registry o1;
    r += o1.save_notice(1);
    r += o1.data(7u);
    r += svc::logging::update_policy(3.5, 1);
    r += static_cast<int>(svc::logging::/*@Handle_374_use5*/Handle::Count);
    r += static_cast<int>(svc::util::Validator::None);
    return (unsigned)(r);
}

bool bind_session(int p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::util::kLimit14;
    r += svc::logging::scan_shipment(1);
    svc::util::Profile d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    svc::logging::/*@Registry_413_use11*/Registry o3;
    r += o3.save_notice(1);
    r += o3.mass;
    r += o3.data(7u);
    svc::logging::/*@Registry_413_use12*/Registry o4;
    r += o4.combine_gateway(2L);
    r += o4.combine_gateway(1);
    r += o4.combine_gateway(2L);
    svc::util::/*@Provider_67_use4*/Provider d5;
    svc::util::/*@Provider_67_use5*/Provider& b6 = d5;
    r += b6.combineShipment(3.5);
    return (bool)(r);
}

long update(long p0) {
    double r = 0;
    svc::logging::/*@Real72_411_use0*/Real72 a1 = 1;
    r += a1;
    svc::util::Buffer o2;
    r += o2.delta;
    r += svc::util::Buffer::/*@value_28_use1*/value(3.5, 1);
    r += o2.combineShipment(3.5);
    r += count(1, 3.5);
    r += svc::logging::count(1, 3.5);
    r += svc::logging::flush(1);
    return (long)(r);
}

int measure_contract(double p0, int p1) {
    double r = 0;
    r += p1;
    r += svc::logging::kMargin71;
    svc::util::/*@Provider_67_use6*/Provider d1;
    svc::util::Buffer* b2 = &d1;
    r += b2->combineShipment(3.5);
    return (int)(r);
}

bool flush_profile(int p0) {
    double r = 0;
    r += p0;
    r += svc::logging::scan_shipment(1);
    svc::logging::/*@Registry_413_use13*/Registry o1;
    r += o1.mass;
    r += o1.combine_gateway(1);
    r += update_policy(3.5, 1);
    svc::util::Buffer d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    svc::util::Buffer o4;
    r += o4.scale;
    r += o4./*@combineCustomer_27_use3*/combineCustomer(1, 1);
    r += o4.combineShipment(3.5);
    r += svc::logging::count(1, 3.5);
    return (bool)(r);
}


}  // namespace logging
}  // namespace svc
