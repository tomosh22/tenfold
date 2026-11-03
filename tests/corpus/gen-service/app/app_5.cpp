#include "app.h"
#include "storage/storage.h"
#include "domain/domain.h"
#include "config/funcs_job.h"
#include "repo/frozen_repository_customer.h"
#include "util/static_ledger.h"
#include "storage/address2.h"
#include "logging/types_mapper.h"
#include "repo/types_product.h"
#include "repo/fast_product.h"
#include "storage/base.h"
#include "repo/funcs_job.h"
#include "cache/funcs_route_272.h"
#include "domain/stable_event.h"
#include "domain/nested_endpoint.h"
#include "cache/node.h"
#include "config/entry.h"
#include "domain/types_validator.h"
#include "repo/macros_task.h"
#include "logging/frozen_endpoint.h"
#include "util/queue2.h"
#include "cache/macros_task.h"
#include "config/impl.h"
#include "logging/dynamic_contract.h"
#include "logging/masked_event_index.h"
#include "config/cart.h"
#include "logging/radial_index_repository.h"
#include "cache/product2.h"
#include "domain/queue_handler.h"
#include "cache/helper.h"
#include "config/active_audit.h"
#include "repo/funcs_queue.h"
#include "repo/tiled_rule.h"
#include "logging/impl.h"
#include "cache/invoice.h"
#include "util/buffer.h"
#include "storage/funcs_endpoint.h"
#include "storage/funcs_address.h"
#include "cache/funcs_route.h"
#include "config/types_contract.h"
#include "storage/policy.h"
#include "config/table51.h"
#include "cache/buffer.h"
#include "cache/config.h"
#include "logging/funcs_policy.h"
#include "config/funcs_report.h"
#include "domain/funcs_account.h"
#include "config/macros_account.h"
#include "config/types_tenant.h"
#include "storage/types_token.h"
#include "logging/registry.h"
#include "storage/macros_schedule.h"
#include "cache/types_invoice.h"
#include "domain/types_quota.h"
#include "storage/stable_report.h"

double appRun284() {
    double r = 0;
    r += svc::config::filter_validator(1, 3.5);
    svc::repo::FrozenRepositoryCustomer d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    r += svc::storage::/*@decodeSession_1027_use1*/decodeSession(2.5);
    r += svc::logging::kScale67;
    r += svc::repo::kLimit185;
    svc::storage::Address2<int> t3;
    r += t3.get();
    t3.set(3);
    svc::repo::FastProduct o4;
    r += o4.ratio;
    r += o4.scanAddress(1);
    svc::storage::/*@Base_1000_use3*/Base o5;
    r += /*@Base_1000_var3*/o5.findTenant(3.5, 1);
    r += svc::repo::filterNotice(1);
    svc::storage::/*@Address2_1023_use1*/Address2<bool> t6;
    r += t6.get();
    t6.set(true);
    r += svc::cache::find(3.5);
    svc::domain::StableEvent o7;
    r += o7.count(3.5);
    r += o7.level;
    svc::domain::NestedEndpoint o8;
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    svc::cache::Node::shared_width++;
    svc::config::Entry d9;
    svc::config::Entry* b10 = &d9;
    r += b10->verify_audit(1, 1);
    r += svc::domain::kStep113;
    svc::storage::/*@Base_1000_use4*/Base o11;
    r += svc::storage::/*@Base_1000_use5*/Base::/*@name_1002_use0*/name(3.5);
    r += o11.findTenant(3.5, 1);
    r += REPO_SCALE_199(2);
    svc::logging::FrozenEndpoint d12;
    svc::util::Queue2* b13 = &d12;
    r += b13->combineCustomer(1, 1);
    r += svc::cache::loadInvoice251(1);
    r += svc::domain::kStep112;
    svc::repo::Handle187 a14 = 1;
    r += a14;
    return r;
}

double appRun285() {
    double r = 0;
    svc::config::Impl o1;
    r += o1.phase;
    r += o1./*@rank_323_use0*/rank;
    svc::logging::DynamicContract d2;
    svc::logging::MaskedEventIndex& b3 = d2;
    r += b3./*@visit_order_466_use0*/visit_order(1, 3.5);
    r += svc::config::collect_token(2.5);
    svc::logging::RadialIndexRepository o4;
    r += o4.mass;
    r += o4.mass;
    svc::cache::Product2 o5;
    r += o5.level;
    r += o5.apply(1);
    svc::domain::QueueHandler o6;
    r += o6./*@visit_order_713_use0*/visit_order(1, 3.5);
    r += o6.bind_notice();
    r += svc::cache::Helper::shared_limit;
    svc::domain::NestedEndpoint o7;
    r += o7.build_endpoint();
    r += o7.build_endpoint();
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    svc::config::ActiveAudit o8;
    r += o8.add();
    r += svc::repo::reset(7u);
    r += svc::repo::computeContract202(1);
    svc::repo::TiledRule o9;
    r += o9.reset(3.5);
    r += o9./*@push_950_use0*/push(2L);
    r += o9.reset(3.5);
    r += svc::logging::Impl::/*@shared_total_477_use0*/shared_total;
    svc::domain::NestedEndpoint d10;
    svc::domain::NestedEndpoint* b11 = &d10;
    r += b11->combineCustomer(1, 1);
    svc::cache::Invoice o12;
    r += o12.rotateBalance(2L);
    r += o12.rotateBalance(2L);
    svc::logging::RadialIndexRepository o13;
    r += o13.level;
    svc::storage::/*@Base_1000_use6*/Base d14;
    svc::util::Buffer& b15 = d14;
    r += b15.combineShipment(3.5);
    r += svc::storage::combineLedger(1, 3.5);
    r += svc::storage::sweepJob(3.5, 1);
    r += svc::cache::flushBalance(3.5, 1);
    return r;
}

double /*@appRun286_1363_def0*/appRun286() {
    double r = 0;
    r += svc::cache::data(3.5, 1);
    r += svc::config::DynamicSchedule_Red;
    svc::storage::/*@Policy_1045_use1*/Policy o1;
    r += /*@Policy_1045_var1*/o1.mergeAudit(1);
    r += o1.mergeAudit(1);
    r += svc::config::/*@table51_fn_11_276_use0*/table51_fn_11(1);
    svc::cache::Buffer d2;
    svc::cache::Config& b3 = d2;
    r += b3.scanNotice(2L);
    r += svc::storage::/*@decodeSession_1027_use2*/decodeSession<int>(2);
    svc::domain::QueueHandler o4;
    r += o4.data(3.5);
    r += o4.resolve_payment(1, 3.5);
    r += o4.reset_task(7u);
    svc::domain::NestedEndpoint o5;
    r += o5.collect_tariff(3.5, 1);
    r += o5./*@scan_token_624_use3*/scan_token(3.5, 1);
    r += o5.combineCustomer(1, 1);
    r += svc::logging::scan_shipment(1);
    svc::config::Real43 a6 = 1;
    r += a6;
    r += svc::config::flush(1, 3.5);
    r += svc::domain::reset(3.5, 1);
    r += /*@CONFIG_SHIPMENT_31_198_use0*/CONFIG_SHIPMENT_31;
    r += svc::config::Config_Red;
    svc::storage::/*@Address2_1023_use2*/Address2<bool> t7;
    r += t7.get();
    t7.set(true);
    r += svc::storage::combineLedger(1);
    r += svc::storage::kMargin210;
    svc::logging::/*@Registry_413_use4*/Registry o8;
    r += /*@Registry_413_var4*/o8.data(7u);
    r += o8.combine_gateway(2L);
    r += o8.data(7u);
    r += svc::storage::selectAccount234(1);
    r += svc::cache::kMargin255;
    svc::logging::/*@Registry_413_use5*/Registry o9;
    r += /*@Registry_413_var5*/o9.combine_invoice(3.5, 1);
    r += o9.combine_gateway(2L);
    r += svc::domain::kLimit103;
    svc::storage::/*@StableReport_1005_use1*/StableReport o10;
    r += svc::storage::/*@StableReport_1005_use2*/StableReport::markToken(3.5);
    r += o10.combineShipment(3.5);
    r += o10.save(1);
    r += svc::storage::combineLedger(1, 3.5);
    return r;
}

