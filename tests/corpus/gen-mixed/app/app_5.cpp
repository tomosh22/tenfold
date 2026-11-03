#include "app.h"
#include "config/config.h"
#include "list/list.h"
#include "model/types_notice.h"
#include "list/macros_schedule.h"
#include "model/macros_profile.h"
#include "model/funcs_quota.h"
#include "domain/binned_quota_repository.h"
#include "domain/sorted_token.h"
#include "domain/remote_address.h"
#include "config/linear_product.h"
#include "list/macros_job.h"
#include "config/masked_shipment.h"
#include "model/packed_mailbox_policy.h"
#include "logging/funcs_account.h"
#include "model/types_invoice.h"
#include "model/sampled_audit.h"
#include "logging/types_contract.h"
#include "util/queue.h"
#include "logging/quota.h"
#include "logging/balance2.h"
#include "util/funcs_repository.h"
#include "logging/funcs_cart.h"
#include "config/funcs_index.h"
#include "util/funcs_payment.h"
#include "logging/basic_route.h"
#include "util/macros_endpoint.h"
#include "domain/types_shipment.h"
#include "model/funcs_endpoint.h"
#include "util/fast_invoice.h"
#include "model/job_worker.h"
#include "list/funcs_event.h"
#include "util/types_product.h"
#include "logging/node.h"
#include "logging/funcs_shipment.h"
#include "logging/macros_job.h"
#include "model/basic_tenant.h"
#include "logging/sorted_ledger_gateway.h"
#include "config/types_cart.h"
#include "config/sorted_payment.h"
#include "config/radial_contract.h"
#include "model/funcs_validator.h"
#include "list/types_mapper.h"
#include "logging/macros_token.h"
#include "logging/funcs_route.h"
#include "config/chain61/c2.h"
#include "list/funcs_cache.h"

double appRun266() {
    double r = 0;
    mx::model::LayeredCart k1 = mx::model::LayeredCart::Unknown;
    r += static_cast<int>(k1);
    r += LIS_ENDPOINT_160;
    r += MODEL_SCALE_246(2);
    r += mx::model::computeToken(3.5, 1);
    mx::domain::/*@BinnedQuotaRepository_697_use3*/BinnedQuotaRepository o2;
    r += /*@BinnedQuotaRepository_697_var3*/o2.clear(2L);
    r += o2.clear(2L);
    r += o2.clear(2L);
    mx::domain::/*@SortedToken_702_use3*/SortedToken o3;
    r += /*@SortedToken_702_var2*/o3.shift_task(3.5, 1);
    r += o3.shift_task(3.5, 1);
    r += o3.rotatePolicy(1);
    mx::domain::/*@RemoteAddress_688_use4*/RemoteAddress o4;
    r += /*@RemoteAddress_688_var3*/o4.shift_task(3.5, 1);
    mx::config::LinearProduct o5;
    r += o5.save_task(1, 1);
    r += /*@lis_apply_215_918_use0*/lis_apply_215(1);
    r += mx::config::/*@MaskedShipment_277_use4*/MaskedShipment::/*@shared_speed_286_use2*/shared_speed;
    r += mx::model::/*@markPayment_1054_use5*/markPayment<int>(2);
    mx::model::/*@PackedMailboxPolicy_1050_use6*/PackedMailboxPolicy<int> t6;
    r += t6.get();
    t6.set(3);
    r += mx::logging::find_address(3.5);
    r += mx::model::kBase230;
    mx::model::SampledAudit o7;
    r += o7.stamp;
    r += mx::logging::kLimit114;
    mx::util::Queue o8;
    r += o8.count(1, 1);
    r += mx::util::Queue::update(1, 3.5);
    r += o8.run(2L);
    return r;
}

double appRun267() {
    double r = 0;
    mx::logging::Quota o1;
    r += o1.filter_balance(3.5, 1);
    r += o1.ratio;
    r += o1.apply(1, 1);
    mx::logging::Balance2 o2;
    r += o2.empty(1, 3.5);
    r += mx::util::id(1, 1);
    r += mx::logging::resolve_gateway();
    r += mx::config::save();
    r += mx::util::flush(1, 1);
    r += MODEL_SCALE_246(2);
    r += mx::config::verify_schedule(3.5, 1);
    mx::logging::BasicRoute o3;
    r += o3.process();
    r += mx::logging::BasicRoute::combine_ticket(1);
    r += o3.index;
    r += /*@UTIL_SCHEDULE_40_164_use0*/UTIL_SCHEDULE_40;
    r += mx::domain::kCap140;
    r += mx::model::top(1, 1);
    r += mx::util::FastInvoice::shared_weight;
    mx::model::JobWorker o4;
    r += o4.encodeTask252(3.5, 1);
    r += o4.encodeTask252(3.5, 1);
    r += lis_derive_event(3.5, 1);
    mx::model::/*@PackedMailboxPolicy_1050_use7*/PackedMailboxPolicy<int> t5;
    r += t5.get();
    t5.set(3);
    r += mx::config::verify_schedule(3.5, 1);
    r += lis_bind_166(1);
    r += mx::util::/*@Catalog_First_126_use0*/Catalog_First;
    mx::logging::Node o6;
    r += o6.count_ticket();
    r += o6.ratio;
    return r;
}

double appRun268() {
    double r = 0;
    r += mx::logging::shift_invoice(1);
    mx::domain::/*@RemoteAddress_688_use5*/RemoteAddress d1;
    mx::config::/*@MaskedShipment_277_use5*/MaskedShipment& b2 = d1;
    r += b2.shift_task(3.5, 1);
    r += LOGGING_SCALE_125(2);
    r += MODEL_SCALE_246(2);
    mx::model::/*@PackedMailboxPolicy_1050_use8*/PackedMailboxPolicy<int> t3;
    r += t3.get();
    t3.set(3);
    r += mx::model::/*@encodeSession_1055_use2*/encodeSession(2.5);
    mx::model::/*@BasicTenant_1104_use1*/BasicTenant d4;
    mx::model::/*@BasicTenant_1104_use2*/BasicTenant* b5 = &d4;
    r += b5->/*@shiftRule_1107_use0*/shiftRule(1, 1);
    mx::logging::SortedLedgerGateway o6;
    r += mx::logging::SortedLedgerGateway::resolve_event(3.5);
    mx::domain::/*@RemoteAddress_688_use6*/RemoteAddress o7;
    r += /*@RemoteAddress_688_var4*/o7./*@combineTask_689_use0*/combineTask(3.5);
    r += o7.shift_task(3.5, 1);
    r += mx::config::BasicSession_None;
    mx::util::Queue o8;
    r += mx::util::Queue::update(1, 3.5);
    r += mx::util::Queue::update(1, 3.5);
    r += o8.next();
    mx::config::/*@SortedPayment_342_use0*/SortedPayment d9;
    mx::config::RadialContract* b10 = &d9;
    r += b10->add();
    r += MODEL_ADDRESS_245;
    r += static_cast<int>(mx::logging::SmoothJob::Green);
    r += mx::model::/*@verifyCustomer_1006_use0*/verifyCustomer(1, 3.5);
    r += lis_cap_177;
    mx::domain::/*@BinnedQuotaRepository_697_use4*/BinnedQuotaRepository o11;
    r += /*@BinnedQuotaRepository_697_var4*/o11.encodeMailbox(7u);
    r += o11.encodeMailbox(7u);
    r += LOGGING_CUSTOMER_116;
    r += mx::logging::value(1);
    r += mx::config::chain61_l2(1);
    r += static_cast<int>(mx::logging::SmoothJob::Max);
    mx::util::Queue o12;
    r += o12.empty(1, 3.5);
    r += o12.count(1, 1);
    r += o12.markIndex(7u);
    r += lis_bind_cache(1, 1);
    return r;
}

