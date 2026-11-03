#include "app.h"
#include "logging/logging.h"
#include "config/config.h"
#include "list/types_handler.h"
#include "model/funcs_endpoint.h"
#include "domain/funcs_address.h"
#include "model/packed_mailbox_policy.h"
#include "config/local_handler_mapper.h"
#include "util/macros_endpoint.h"
#include "util/queue.h"
#include "util/funcs_job.h"
#include "domain/funcs_event.h"
#include "domain/types_shipment.h"
#include "logging/types_validator.h"
#include "domain/remote_address.h"
#include "util/basic_order.h"

double appRun260() {
    double r = 0;
    lis_size_157_t a1 = 1;
    r += a1;
    r += mx::model::top(1, 1);
    r += mx::domain::empty(3.5, 1);
    r += mx::model::/*@markPayment_1054_use1*/markPayment(2.5);
    mx::config::LocalHandlerMapper o2;
    r += o2.flags;
    r += o2.reset(2L);
    mx::model::/*@PackedMailboxPolicy_1050_use2*/PackedMailboxPolicy<int> t3;
    r += t3.get();
    t3.set(3);
    r += UTIL_SCALE_43(2);
    mx::util::Queue d4;
    mx::util::Queue& b5 = d4;
    r += b5.empty(1, 3.5);
    mx::util::Queue o6;
    r += o6.markIndex(7u);
    r += o6.markIndex(7u);
    r += mx::util::top(1);
    r += mx::model::id(7u);
    r += mx::domain::rotateMailbox();
    r += mx::domain::kBase139;
    mx::logging::Quota2 k7 = mx::logging::Quota2::Unknown;
    r += static_cast<int>(k7);
    mx::domain::/*@RemoteAddress_688_use1*/RemoteAddress o8;
    r += /*@RemoteAddress_688_var1*/o8.top(2L);
    r += o8.top(2L);
    r += o8.offset;
    mx::util::Queue d9;
    mx::util::BasicOrder* b10 = &d9;
    r += b10->empty(1, 3.5);
    return r;
}

