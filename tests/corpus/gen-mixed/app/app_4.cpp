#include "app.h"
#include "domain/domain.h"
#include "util/util.h"
#include "domain/scoped_job.h"
#include "config/local_handler_mapper.h"
#include "model/funcs_endpoint.h"
#include "config/types_cart.h"
#include "model/table257.h"
#include "util/balance.h"
#include "util/basic_order.h"
#include "domain/funcs_address.h"
#include "model/types_invoice.h"
#include "domain/sorted_token.h"
#include "domain/types_shipment.h"
#include "model/packed_mailbox_policy.h"
#include "logging/funcs_account.h"
#include "util/queue.h"
#include "list/types_handler.h"
#include "logging/macros_job.h"
#include "util/fast_invoice.h"
#include "model/basic_tenant.h"
#include "logging/macros_token.h"

double appRun265() {
    double r = 0;
    mx::domain::ScopedJob o1;
    r += o1.count(1);
    mx::config::LocalHandlerMapper d2;
    mx::config::LocalHandlerMapper* b3 = &d2;
    r += b3->split_ledger();
    r += mx::model::filterQuota(1, 3.5);
    r += mx::config::BasicSession_None;
    r += mx::model::table257_fn_8(1);
    mx::util::Balance d4;
    mx::util::BasicOrder& b5 = d4;
    r += b5.count(1, 1);
    r += mx::domain::flushRule(1, 1);
    r += mx::model::kCap229;
    mx::domain::/*@SortedToken_702_use2*/SortedToken o6;
    r += /*@SortedToken_702_var1*/o6.deriveRepository(3.5);
    r += o6.save(1, 3.5);
    r += mx::domain::kCap141;
    r += mx::model::/*@markPayment_1054_use4*/markPayment<int>(2);
    r += mx::config::BasicSession_Green;
    r += mx::logging::reset_route(2L);
    mx::util::Queue o7;
    r += o7.count(1, 1);
    r += /*@lis_cap_156_733_use0*/lis_cap_156;
    r += mx::logging::filterProduct128(1);
    r += mx::util::FastInvoice::shared_weight;
    mx::util::Queue o8;
    r += mx::util::Queue::update(1, 3.5);
    mx::model::/*@BasicTenant_1104_use0*/BasicTenant o9;
    r += /*@BasicTenant_1104_var0*/o9.updateRoute(1, 1);
    r += mx::model::filterQuota(1, 3.5);
    r += LOGGING_CUSTOMER_116;
    return r;
}

