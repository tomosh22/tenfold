#include "model/entry.h"
#include "model/config.h"
#include "model/table146.h"
#include "model/types_repository.h"
#include "domain/funcs_notice.h"
#include "model/address.h"
#include "domain/types_mailbox.h"
#include "domain/queue_handler.h"
#include "logging/masked_event_index.h"
#include "domain/macros_quota.h"

namespace svc::model {

int Entry::shared_total = 0;

bool Entry::probeQuota(long p0) {
    double r = 0;
    r += p0;
    svc::model::Config o1;
    r += o1.limit157;
    r += o1.sortTask(2L);
    r += this->phase;
    return (bool)(r);
}

unsigned Entry::add(long p0) {
    double r = 0;
    r += svc::model::table146_fn_1(1);
    r += svc::model::SharedContract_First;
    r += svc::domain::scale_mailbox(1, 1);
    r += svc::model::kLimit141;
    r += svc::model::table146_fn_1(1);
    return (unsigned)(r);
}

int Entry::name(double p0, int p1) {
    double r = 0;
    r += p0;
    svc::model::/*@Address_771_use5*/Address o1;
    r += /*@Address_771_var2*/o1.combineCart(3.5, 1);
    r += stamp;
    r += measureRule(3.5, 1);
    r += add(2L);
    return (int)(r);
}

bool Entry::measureRule(double p0, int p1) {
    double r = 0;
    r += p1;
    svc::model::/*@Address_771_use6*/Address o1;
    r += /*@Address_771_var3*/o1.selectTask(3.5, 1);
    r += o1.findRepository(1, 1);
    r += o1.combineCart(3.5, 1);
    svc::model::/*@Address_771_use7*/Address o2;
    r += /*@Address_771_var4*/o2.combineCart(3.5, 1);
    r += o2.selectTask(3.5, 1);
    r += o2.findRepository(1, 1);
    r += svc::model::kLimit141;
    r += svc::model::kLimit141;
    r += svc::domain::kMargin96;
    return (bool)(r);
}

int Entry::apply(double p0, int p1) const {
    double r = 0;
    r += p0;
    r += svc::model::kLimit141;
    r += svc::model::State_Green;
    r += stamp;
    svc::domain::QueueHandler d1;
    svc::logging::MaskedEventIndex* b2 = &d1;
    r += b2->reset_task(7u);
    r += DOMAIN_SESSION_116;
    return (int)(r);
}


}  // namespace svc::model
