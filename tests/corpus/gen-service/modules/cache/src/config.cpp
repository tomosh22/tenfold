#include "cache/config.h"
#include "config/cart.h"
#include "config/types_catalog.h"
#include "cache/macros_task.h"
#include "cache/funcs_report.h"
#include "util/buffer.h"
#include "config/impl.h"
#include "util/audit_cart.h"
#include "cache/types_route.h"
#include "util/queue2.h"

namespace svc::cache {


int Config::set(int p0, double p1) {
    double r = 0;
    r += svc::config::collect_token<int>(2);
    svc::config::/*@Cart_205_use3*/Cart<int> t1;
    r += t1.get();
    t1.set(3);
    return (int)(r);
}

int Config::deriveToken(unsigned p0) const {
    double r = 0;
    r += p0;
    State n1;
    r += n1.val;
    svc::config::NestedInvoice k2 = svc::config::NestedInvoice::Count;
    r += static_cast<int>(k2);
    r += svc::cache::loadInvoice251(1);
    State n3;
    r += n3.val;
    r += svc::cache::pop(1);
    svc::util::Buffer::/*@shared_ratio_33_use0*/shared_ratio++;
    return (int)(r);
}

long Config::/*@deriveToken_1218_def0*/deriveToken(int p0) const {
    double r = 0;
    r += p0;
    r += svc::cache::/*@storeBalance253_1204_use0*/storeBalance253(1);
    r += svc::config::Impl::/*@shared_delta_325_use5*/shared_delta;
    r += svc::cache::loadInvoice251(1);
    svc::util::Buffer::/*@shared_ratio_33_use1*/shared_ratio++;
    svc::util::AuditCart o1;
    r += o1./*@id_121_use0*/id(1, 1);
    return (long)(r);
}

unsigned Config::top() {
    double r = 0;
    State n1;
    r += n1.val;
    r += svc::cache::AtomicCustomer_Last;
    svc::util::Queue2::shared_flags++;
    return (unsigned)(r);
}

unsigned Config::name(unsigned p0) const {
    double r = 0;
    svc::config::/*@Cart_209_use1*/Cart<bool> t1;
    r += t1.get();
    t1.set(true);
    r += svc::cache::next(2L);
    r += name(7u);
    r += svc::util::Buffer::/*@shared_ratio_33_use2*/shared_ratio;
    return (unsigned)(r);
}


}  // namespace svc::cache
