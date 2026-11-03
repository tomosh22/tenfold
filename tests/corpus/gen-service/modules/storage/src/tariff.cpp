#include "storage/tariff.h"
#include "storage/address2.h"
#include "storage/funcs_endpoint.h"
#include "storage/funcs_session.h"
#include "config/types_contract.h"
#include "util/types_profile_13.h"
#include "storage/macros_tenant.h"
#include "util/radial_policy.h"

namespace svc {
namespace storage {

static int scale(int p0) { return p0 + 4; }

double Tariff::rotateOrder(unsigned p0) {
    double r = 0;
    svc::storage::Address2<int> t1;
    r += t1.get();
    t1.set(3);
    r += scale(1);
    r += svc::storage::resetMapper(1, 3.5);
    return (double)(r);
}

int Tariff::deriveIndex(int p0) const {
    double r = 0;
    r += depth;
    r += deriveIndex(1);
    svc::storage::/*@Address2_1023_use4*/Address2<bool> t1;
    r += t1.get();
    t1.set(true);
    return (int)(r);
}

bool Tariff::add(unsigned p0) const {
    double r = 0;
    r += p0;
    r += svc::storage::filterInvoice(1, 3.5);
    r += items237.size();
    r += svc::config::kCap42;
    svc::util::Real17 a1 = 1;
    r += a1;
    return (bool)(r);
}

unsigned Tariff::shiftOrder(double p0) const {
    double r = 0;
    r += p0;
    r += svc::storage::buildContract227(1);
    svc::util::/*@RadialPolicy_175_use11*/RadialPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::storage::combineLedger(1);
    r += svc::storage::resetMapper(1, 3.5);
    return (unsigned)(r);
}


}  // namespace storage
}  // namespace svc
