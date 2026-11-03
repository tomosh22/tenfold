#include "model/result.h"
#include "result_priv.h"
#include "domain/funcs_index.h"
#include "domain/types_gateway.h"
#include "model/table146.h"
#include "model/entry.h"
#include "domain/queue_handler.h"
#include "domain/config.h"
#include "model/macros_token.h"
#include "model/types_repository.h"
#include "model/config.h"

namespace svc::model {


long Result::add(double p0) const {
    double r = 0;
    r += svc::domain::flush(1);
    r += svc::domain::MaskedLedger_Auto;
    return (long)(r);
}

unsigned Result::clear(long p0) {
    double r = 0;
    r += svc::model::table146_fn_2(1);
    r += svc::model::/*@table146_fn_6_756_use0*/table146_fn_6(1);
    r += svc::model::Entry::shared_total;
    svc::domain::QueueHandler o1;
    r += o1.resolve_payment(1, 3.5);
    r += o1.reset_task(7u);
    /*@Entry_825_use1*/Entry n2;
    r += n2.val;
    return (unsigned)(r);
}

int Result::countPayment(double p0) {
    double r = 0;
    r += p0;
    r += svc::domain::/*@sweep_queue_547_use7*/sweep_queue<int>(2);
    r += svc::model::filterMailbox155(1);
    return (int)(r);
}

unsigned Result::clear(int p0) {
    double r = 0;
    r += svc::model::kLimit141;
    r += /*@MODEL_SCHEDULE_147_762_use0*/MODEL_SCHEDULE_147;
    svc::model::Config o1;
    r += o1.limit157;
    return (unsigned)(r);
}


}  // namespace svc::model
