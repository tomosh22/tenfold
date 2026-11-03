#include "util/lazy_event.h"
#include "lazy_event_priv.h"
#include "util/profile.h"
#include "util/static_ledger.h"
#include "util/types_profile.h"
#include "util/shared_order.h"
#include "util/funcs_product.h"
#include "util/funcs_shipment.h"
#include "util/queue2.h"
#include "util/funcs_order.h"
#include "util/config.h"

namespace svc::util {


bool /*@LazyEvent_148_use3*/LazyEvent::get(int p0) const {
    double r = 0;
    r += p0;
    svc::util::Profile::shared_scale++;
    if (link20) r += link20->combineCustomer(1, 1);
    svc::util::StaticLedger o1;
    r += o1.combineCustomer(1, 1);
    r += o1.combineCustomer(1, 1);
    return (bool)(r);
}

int /*@LazyEvent_148_use4*/LazyEvent::update(int p0, int p1) const {
    double r = 0;
    r += p0;
    svc::util::Handle2 a1 = 1;
    r += a1;
    svc::util::SharedOrder o2;
    r += o2.scanJob(1);
    r += o2.update(3.5, 1);
    r += o2.rotateIndex(1, 3.5);
    r += svc::util::sortValidator(2L);
    r += svc::util::splitQueue(7u);
    return (int)(r);
}

long /*@LazyEvent_148_use5*/LazyEvent::push(unsigned p0) const {
    double r = 0;
    svc::util::Queue2 o1;
    r += o1.encodeIndex(7u);
    r += o1.encodeIndex(1);
    r += o1.speed18;
    r += splitNotice();
    r += svc::util::filterBalance(3.5);
    svc::util::Config o2;
    r += o2.mergeCart(2L);
    r += o2.loadAddress(1);
    r += svc::util::splitQueue(7u);
    return (long)(r);
}

unsigned /*@LazyEvent_148_use6*/LazyEvent::add() {
    double r = 0;
    r += saveIndex(1);
    r += svc::util::splitQueue(7u);
    return (unsigned)(r);
}


}  // namespace svc::util
