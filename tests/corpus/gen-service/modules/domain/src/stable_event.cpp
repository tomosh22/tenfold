#include "domain/stable_event.h"
#include "logging/chain95/c2.h"
#include "domain/types_mailbox.h"
#include "domain/funcs_profile.h"
#include "domain/funcs_quota.h"
#include "util/buffer.h"
#include "domain/funcs_contract.h"
#include "util/funcs_order.h"
#include "domain/config.h"
#include "logging/impl.h"
#include "domain/queue_handler.h"

namespace svc::domain {

static int clamp(int p0) { return p0 + 7; }

long StableEvent::select_repository(double p0, int p1) const {
    double r = 0;
    r += p1;
    r += svc::logging::chain95_l2(1);
    r += svc::domain::kMargin96;
    return (long)(r);
}

bool StableEvent::scale_repository(int p0, int p1) {
    double r = 0;
    r += p0;
    r += svc::domain::set(7u);
    r += svc::domain::count_quota(1, 3.5);
    r += svc::util::Buffer::/*@shared_ratio_33_use7*/shared_ratio;
    r += select_repository(3.5, 1);
    return (bool)(r);
}

bool StableEvent::compute_mapper(unsigned p0) {
    double r = 0;
    r += svc::domain::store_customer(7u);
    r += svc::domain::store_customer(7u);
    r += svc::util::scaleTicket(3.5, 1);
    svc::domain::/*@Config_543_use14*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    svc::logging::Impl::/*@shared_total_477_use2*/shared_total++;
    return (bool)(r);
}

long StableEvent::next(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += select_repository(3.5, 1);
    r += svc::domain::push(3.5, 1);
    r += this->level;
    r += select_repository(3.5, 1);
    svc::domain::QueueHandler o1;
    r += svc::domain::QueueHandler::value(1, 1);
    r += o1./*@visit_order_713_use1*/visit_order(1, 3.5);
    r += o1.empty(7u);
    svc::domain::QueueHandler o2;
    r += o2.id(3.5);
    r += o2.reset_task(7u);
    r += o2./*@visit_order_713_use2*/visit_order(1, 3.5);
    return (long)(r);
}


}  // namespace svc::domain
