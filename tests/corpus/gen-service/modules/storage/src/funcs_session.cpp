#include "storage/funcs_session.h"
#include "storage/base.h"
#include "storage/stable_report.h"
#include "storage/job2.h"
#include "storage/address2.h"
#include "storage/macros_tenant.h"
#include "storage/funcs_endpoint.h"
#include "storage/types_payment.h"
#include "util/provider.h"
#include "config/active_audit.h"
#include "util/types_session.h"
#include "config/macros_route.h"
#include "storage/types_endpoint.h"

namespace svc {
namespace storage {

double splitShipment(double p0) {
    double r = 0;
    svc::storage::/*@Base_1000_use19*/Base o1;
    r += svc::storage::/*@Base_1000_use20*/Base::/*@name_1002_use8*/name(3.5);
    r += o1.rank;
    r += o1.rank;
    svc::storage::/*@StableReport_1005_use9*/StableReport o2;
    r += /*@StableReport_1005_var5*/o2.ratio;
    r += o2.flush(3.5, 1);
    r += o2.flush(3.5, 1);
    svc::storage::Job2 o3;
    r += o3.apply(3.5, 1);
    r += o3.flushSession(1, 3.5);
    r += o3.apply(3.5, 1);
    return (double)(r);
}

long sweepOrder(int p0, double p1) {
    double r = 0;
    r += p1;
    svc::storage::/*@Address2_1023_use3*/Address2<bool> t1;
    r += t1.get();
    t1.set(true);
    r += rotateSchedule226(1);
    r += svc::storage::/*@decodeSession_1027_use3*/decodeSession<int>(2);
    return (long)(r);
}

bool filterInvoice(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::storage::combineLedger(1, 3.5);
    r += svc::storage::resetMapper(1, 3.5);
    r += svc::storage::Payment_Unknown;
    r += combineLedger(1);
    r += svc::storage::resetMapper(1, 3.5);
    svc::storage::/*@Base_1000_use21*/Base o1;
    r += o1.rank;
    return (bool)(r);
}

double filterInvoice() {
    double r = 0;
    svc::storage::/*@Base_1000_use22*/Base o1;
    r += svc::storage::/*@Base_1000_use23*/Base::/*@name_1002_use9*/name(3.5);
    svc::storage::/*@StableReport_1005_use10*/StableReport d2;
    svc::util::/*@Provider_67_use8*/Provider& b3 = d2;
    r += b3.combineShipment(3.5);
    svc::storage::/*@Base_1000_use24*/Base o4;
    r += o4.findTenant(3.5, 1);
    r += svc::storage::/*@Base_1000_use25*/Base::/*@name_1002_use10*/name(3.5);
    svc::config::ActiveAudit o5;
    r += o5.run(2L);
    svc::storage::/*@Base_1000_use26*/Base o6;
    r += svc::storage::/*@Base_1000_use27*/Base::/*@name_1002_use11*/name(3.5);
    r += svc::storage::/*@Base_1000_use28*/Base::/*@name_1002_use12*/name(3.5);
    return (double)(r);
}

int size(int p0, int p1) {
    double r = 0;
    r += p0;
    svc::storage::Address2<int> t1;
    r += t1.get();
    t1.set(3);
    svc::util::/*@Id22_173_use0*/Id22 a2 = 1;
    r += a2;
    r += CONFIG_SCALE_54(2);
    r += svc::storage::Payment_Max;
    svc::storage::MaskedValidator k3 = svc::storage::MaskedValidator::Auto;
    r += static_cast<int>(k3);
    return (int)(r);
}


}  // namespace storage
}  // namespace svc
