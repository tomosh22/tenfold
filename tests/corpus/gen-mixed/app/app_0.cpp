#include "app.h"
#include "util/util.h"
#include "list/list.h"
#include "model/sorted_validator.h"
#include "model/packed_mailbox_policy.h"
#include "util/queue.h"
#include "util/basic_order.h"
#include "logging/types_contract.h"
#include "logging/funcs_address.h"
#include "list/funcs_socket.h"
#include "domain/chain145/c0.h"
#include "model/types_notice.h"
#include "model/chain237/c0.h"
#include "model/types_invoice.h"
#include "list/types_handler.h"
#include "logging/types_validator.h"
#include "domain/sorted_token.h"
#include "config/masked_shipment.h"
#include "list/funcs_slot.h"
#include "model/packed_catalog_tariff.h"
#include "config/primary_mapper.h"
#include "config/local_handler_mapper.h"
#include "domain/funcs_event.h"
#include "domain/remote_address.h"
#include "logging/macros_token.h"
#include "domain/funcs_address.h"
#include "list/macros_schedule.h"
#include "util/funcs_endpoint.h"
#include "model/macros_profile.h"
#include "config/types_cart.h"
#include "config/chain61/c2.h"
#include "list/table158.h"

double appRun258() {
    double r = 0;
    mx::model::SortedValidator o1;
    r += o1.height233;
    r += o1.combineAudit(3.5);
    r += o1./*@height_998_use0*/height;
    mx::model::/*@PackedMailboxPolicy_1050_use0*/PackedMailboxPolicy<int> t2;
    r += t2.get();
    t2.set(3);
    mx::util::Queue d3;
    mx::util::BasicOrder* b4 = &d3;
    r += b4->apply();
    mx::logging::SmoothJob k5 = mx::logging::SmoothJob::Default;
    r += static_cast<int>(k5);
    r += mx::logging::kLimit114;
    r += mx::logging::make(1);
    r += lis_sweep_socket(7u);
    r += mx::domain::chain145_entry(1);
    r += mx::model::kStep225;
    mx::model::/*@PackedMailboxPolicy_1050_use1*/PackedMailboxPolicy<int> t6;
    r += t6.get();
    t6.set(3);
    r += mx::model::chain237_l0(1);
    r += mx::model::kBase230;
    r += LIS_GREEN_154;
    r += mx::logging::kCap131;
    mx::domain::/*@SortedToken_702_use0*/SortedToken o7;
    r += /*@SortedToken_702_var0*/o7.save(2L);
    r += o7./*@markEndpoint_703_use0*/markEndpoint(1, 3.5);
    mx::domain::/*@SortedToken_702_use1*/SortedToken d8;
    mx::config::/*@MaskedShipment_277_use0*/MaskedShipment& b9 = d8;
    r += b9.compute_account(3.5, 1);
    r += lis_apply_slot(7u);
    mx::model::PackedCatalogTariff o10;
    r += o10.get(7u);
    mx::config::PrimaryMapper o11;
    r += o11.index;
    r += o11.split_ledger();
    return r;
}

double appRun259() {
    double r = 0;
    mx::config::LocalHandlerMapper d1;
    mx::config::PrimaryMapper* b2 = &d1;
    r += b2->split_ledger();
    r += mx::config::/*@MaskedShipment_277_use1*/MaskedShipment::/*@shared_speed_286_use0*/shared_speed;
    r += mx::domain::sweepIndex(3.5);
    mx::domain::/*@RemoteAddress_688_use0*/RemoteAddress o3;
    r += /*@RemoteAddress_688_var0*/o3.top(2L);
    r += LOGGING_CUSTOMER_116;
    r += mx::domain::scaleQueue(7u);
    mx::model::Size228 a4 = 1;
    r += a4;
    r += mx::domain::flushRule(3.5, 1);
    mx::util::Queue o5;
    r += o5.next();
    r += o5.markIndex(7u);
    r += /*@LIS_SCALE_163_778_use0*/LIS_SCALE_163(2);
    r += mx::domain::findShipment(1, 1);
    r += mx::util::rotateGateway(1, 1);
    r += mx::model::/*@markPayment_1054_use0*/markPayment<int>(2);
    mx::model::SortedValidator o6;
    r += o6.init(3.5);
    r += /*@MODEL_CUSTOMER_244_1074_use0*/MODEL_CUSTOMER_244;
    r += mx::config::BasicSession_Blue;
    r += mx::config::chain61_l2(1);
    r += lis_merge_165(1);
    r += lis_table158_fn_5(1);
    return r;
}

