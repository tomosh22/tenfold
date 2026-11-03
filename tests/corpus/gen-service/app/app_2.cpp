#include "app.h"
#include "domain/domain.h"
#include "storage/storage.h"
#include "config/entry.h"
#include "util/static_ledger.h"
#include "config/types_tenant.h"
#include "logging/radial_index_repository.h"
#include "logging/funcs_balance.h"
#include "repo/fast_product.h"
#include "config/macros_route.h"
#include "storage/base.h"
#include "repo/frozen_repository_customer.h"
#include "cache/funcs_report.h"
#include "config/impl.h"
#include "domain/nested_endpoint.h"
#include "config/active_audit.h"
#include "cache/funcs_invoice.h"
#include "repo/types_product.h"
#include "logging/macros_account.h"
#include "cache/macros_task.h"
#include "storage/types_rule.h"
#include "storage/types_rule_228.h"
#include "storage/funcs_schedule.h"
#include "logging/funcs_ledger.h"
#include "domain/funcs_notice.h"
#include "logging/types_mapper.h"
#include "config/funcs_invoice.h"
#include "config/tiled_tenant.h"
#include "logging/impl.h"
#include "domain/macros_quota.h"
#include "repo/macros_task.h"
#include "storage/address2.h"
#include "logging/registry.h"
#include "domain/funcs_contract.h"
#include "logging/funcs_policy.h"
#include "domain/types_validator.h"
#include "storage/funcs_session.h"
#include "storage/primary_ledger.h"
#include "domain/funcs_report.h"
#include "config/cart.h"
#include "repo/tiled_rule.h"
#include "cache/config.h"
#include "storage/types_payment.h"

double appRun278() {
    double r = 0;
    svc::config::Entry d1;
    svc::util::StaticLedger* b2 = &d1;
    r += b2->combineCustomer(1, 1);
    r += svc::config::/*@Config_Green_190_use0*/Config_Green;
    svc::logging::RadialIndexRepository o3;
    r += o3.mass;
    r += o3.process(1, 3.5);
    r += svc::logging::count(1, 3.5);
    svc::repo::FastProduct o4;
    r += o4.stamp;
    r += o4.find(1, 3.5);
    r += /*@CONFIG_ACCOUNT_52_277_use0*/CONFIG_ACCOUNT_52;
    svc::storage::/*@Base_1000_use1*/Base o5;
    r += /*@Base_1000_var1*/o5.findTenant(3.5, 1);
    r += o5.findTenant(3.5, 1);
    r += o5.findTenant(3.5, 1);
    svc::repo::FrozenRepositoryCustomer d6;
    svc::repo::FrozenRepositoryCustomer* b7 = &d6;
    r += b7->combineCustomer(1, 1);
    r += svc::cache::next(3.5, 1);
    r += svc::config::Impl::/*@shared_delta_325_use3*/shared_delta;
    svc::domain::NestedEndpoint d8;
    svc::domain::NestedEndpoint& b9 = d8;
    r += b9.combineCustomer(1, 1);
    svc::config::ActiveAudit o10;
    r += o10.add();
    return r;
}

double appRun279() {
    double r = 0;
    svc::storage::/*@Base_1000_use2*/Base o1;
    r += /*@Base_1000_var2*/o1.findTenant(3.5, 1);
    r += o1.rank;
    r += o1.rank;
    r += svc::cache::save(1, 3.5);
    r += svc::repo::kScale184;
    r += svc::logging::find_queue();
    r += LOGGING_SCALE_84(2);
    r += svc::cache::loadInvoice251(1);
    r += svc::storage::/*@Context_Blue_1065_use0*/Context_Blue;
    svc::storage::/*@Real230_1130_use0*/Real230 a2 = 1;
    r += a2;
    r += svc::logging::RadialIndexRepository::shared_height;
    r += svc::storage::verifyBalance(1, 1);
    r += svc::logging::apply_queue(1);
    r += svc::domain::split_ledger(1);
    svc::repo::FastProduct o3;
    r += o3.markInvoice(7u);
    r += o3.stamp;
    r += o3.score;
    r += svc::logging::kScale67;
    r += svc::config::bind_session(1, 1);
    svc::config::TiledTenant o4;
    r += o4.process(2L);
    r += o4.limit;
    r += o4.sweep_endpoint(1, 3.5);
    svc::logging::Impl o5;
    r += o5.flags;
    r += o5.fetch_rule(3.5);
    r += DOMAIN_SESSION_116;
    r += REPO_CART_197;
    r += svc::logging::count(1, 3.5);
    svc::storage::/*@Address2_1023_use0*/Address2<bool> t6;
    r += t6.get();
    t6.set(true);
    return r;
}

double appRun280() {
    double r = 0;
    svc::logging::/*@Registry_413_use1*/Registry o1;
    r += /*@Registry_413_var1*/o1.data(7u);
    r += svc::domain::store_customer(7u);
    r += svc::logging::scan_shipment(1);
    r += static_cast<int>(svc::domain::SampledSchedule::Auto);
    r += svc::config::bind_session(1, 1);
    r += svc::config::Impl::/*@shared_delta_325_use4*/shared_delta;
    r += svc::storage::filterInvoice();
    r += svc::domain::probeProfile121(1);
    svc::storage::/*@PrimaryLedger_1117_use0*/PrimaryLedger o2;
    r += /*@PrimaryLedger_1117_var0*/o2.findWorker(2L);
    r += o2.combineCustomer(1, 1);
    svc::domain::NestedEndpoint o3;
    r += o3.combineCustomer(1, 1);
    r += svc::domain::NestedEndpoint::verify_tenant(1);
    r += svc::domain::apply_product(3.5, 1);
    r += LOGGING_SCALE_83(2);
    svc::config::/*@Cart_205_use2*/Cart<int> t4;
    r += t4.get();
    t4.set(3);
    svc::domain::NestedEndpoint o5;
    r += o5./*@scan_token_624_use1*/scan_token(3.5, 1);
    r += o5./*@mass_628_use0*/mass;
    svc::repo::TiledRule o6;
    r += o6.push();
    r += svc::logging::flush(1);
    svc::domain::NestedEndpoint o7;
    r += o7./*@scan_token_624_use2*/scan_token(3.5, 1);
    r += o7.probe_mapper(1, 3.5);
    svc::logging::/*@Registry_413_use2*/Registry o8;
    r += /*@Registry_413_var2*/o8.save_notice(1);
    r += o8.save_notice(1);
    r += o8.combine_gateway(2L);
    svc::cache::Config o9;
    r += o9.set(1, 3.5);
    r += o9.name(7u);
    r += o9.deriveToken(7u);
    r += svc::storage::Payment_Max;
    r += svc::logging::RadialIndexRepository::shared_height;
    r += svc::domain::kBase111;
    return r;
}

