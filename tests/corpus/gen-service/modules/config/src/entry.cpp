#include "config/entry.h"
#include "config/funcs_job.h"
#include "util/funcs_product.h"
#include "config/types_tenant.h"
#include "config/macros_account.h"
#include "util/config.h"
#include "util/audit_cart.h"
#include "config/impl.h"
#include "config/types_contract.h"
#include "config/funcs_invoice.h"
#include "util/queue2.h"
#include "config/types_session.h"
#include "config/funcs_report.h"

namespace svc {
namespace config {


int Entry::bind_policy(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::config::filter_validator(2L);
    r += encode_product(3.5, 1);
    r += svc::util::encodePayment();
    svc::config::/*@Real29_196_use0*/Real29 a1 = 1;
    r += a1;
    r += CONFIG_SCALE_34(2);
    svc::util::Config o2;
    r += o2./*@find_9_use0*/find(7u);
    r += o2.splitContract(3.5, 1);
    return (int)(r);
}

unsigned Entry::encode_product(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    svc::util::AuditCart o1;
    r += o1./*@score_124_use0*/score;
    svc::config::Impl o2;
    r += o2.set(7u);
    r += svc::config::DynamicSchedule_Red;
    return (unsigned)(r);
}

bool Entry::combineCustomer(int p0, int p1) const {
    double r = 0;
    r += p1;
    r += svc::config::make(7u);
    svc::util::Queue2 o1;
    r += o1.index;
    r += o1.speed18;
    svc::util::Config o2;
    r += o2.width;
    return (bool)(r);
}

bool Entry::reset() {
    double r = 0;
    r += svc::config::measure_queue(1);
    svc::config::Impl::/*@shared_delta_325_use8*/shared_delta++;
    return (bool)(r);
}

double Entry::verify_audit(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += svc::config::get(1, 3.5);
    r += svc::config::/*@ActiveBalance_Blue_296_use0*/ActiveBalance_Blue;
    r += svc::config::scan_queue(7u);
    r += svc::config::split_rule(2L);
    return (double)(r);
}


}  // namespace config
}  // namespace svc
