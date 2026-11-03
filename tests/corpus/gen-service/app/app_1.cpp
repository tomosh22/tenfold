#include "app.h"
#include "repo/repo.h"
#include "domain/domain.h"
#include "storage/funcs_schedule.h"
#include "logging/types_balance.h"
#include "repo/macros_task.h"
#include "config/funcs_invoice.h"
#include "storage/funcs_session.h"
#include "domain/config.h"
#include "domain/macros_quota.h"
#include "config/types_tenant.h"
#include "cache/types_route.h"
#include "repo/sparse_repository.h"
#include "storage/macros_tenant.h"
#include "logging/registry.h"
#include "cache/macros_task.h"
#include "config/table51.h"
#include "storage/types_endpoint.h"
#include "logging/frozen_endpoint.h"
#include "domain/types_validator.h"
#include "cache/atomic_policy.h"
#include "config/macros_account.h"
#include "storage/policy.h"
#include "logging/funcs_ledger.h"
#include "cache/macros_account.h"
#include "domain/nested_endpoint.h"
#include "cache/types_invoice.h"
#include "repo/funcs_queue.h"
#include "cache/node.h"
#include "logging/radial_index_repository.h"
#include "logging/types_mapper.h"
#include "domain/funcs_queue.h"
#include "config/impl.h"
#include "cache/funcs_route_272.h"
#include "domain/funcs_index.h"
#include "config/cart.h"
#include "cache/funcs_report.h"
#include "repo/frozen_repository_customer.h"
#include "config/active_audit.h"
#include "config/types_contract.h"
#include "storage/stable_report.h"
#include "storage/funcs_endpoint.h"
#include "storage/macros_schedule.h"
#include "logging/macros_account.h"
#include "storage/types_token.h"
#include "cache/invoice.h"
#include "logging/funcs_mapper.h"
#include "config/macros_route.h"
#include "domain/types_ledger.h"
#include "repo/fast_product.h"
#include "config/funcs_address.h"
#include "storage/types_payment.h"
#include "logging/notice.h"
#include "logging/route.h"

double appRun275() {
    double r = 0;
    r += svc::storage::findContract238(3.5);
    r += svc::logging::RigidPolicy_Unknown;
    r += REPO_SCALE_199(2);
    r += svc::config::make(7u);
    r += svc::storage::filterInvoice(1, 3.5);
    svc::domain::/*@Config_543_use0*/Config<int> t1;
    r += t1.get();
    t1.set(3);
    r += svc::domain::applyToken122(1);
    r += svc::config::kStep26;
    svc::cache::Size244 a2 = 1;
    r += a2;
    svc::repo::SparseRepository o3;
    r += o3.resetQuota(1, 1);
    r += o3.resetQuota(1, 1);
    r += o3.loadQuota(1, 1);
    r += STORAGE_SCALE_223(2);
    svc::logging::/*@Registry_413_use0*/Registry o4;
    r += /*@Registry_413_var0*/o4.save_notice(1);
    r += svc::cache::loadInvoice251(1);
    r += svc::config::table51_fn_5(1);
    r += svc::storage::kMargin219;
    svc::logging::FrozenEndpoint o5;
    r += o5.sweep_quota(2L);
    r += svc::domain::kBase111;
    svc::cache::AtomicPolicy o6;
    r += o6.flushRepository(1, 1);
    r += o6.countReport(3.5, 1);
    r += STORAGE_SCALE_223(2);
    r += CONFIG_SCALE_34(2);
    r += DOMAIN_SCALE_119(2);
    svc::storage::/*@Policy_1045_use0*/Policy o7;
    r += /*@Policy_1045_var0*/o7.mergeAudit(1);
    r += o7.load(3.5);
    return r;
}

double appRun276() {
    double r = 0;
    r += svc::logging::apply_queue(1);
    r += CACHE_REPOSITORY_262;
    svc::domain::NestedEndpoint o1;
    r += o1./*@scan_token_624_use0*/scan_token(3.5, 1);
    r += svc::cache::kMargin255;
    r += svc::repo::saveMailbox();
    r += svc::cache::Node::shared_width;
    r += svc::logging::RadialIndexRepository::shared_height;
    r += svc::logging::kScale67;
    r += svc::domain::save(1);
    svc::config::Impl::/*@shared_delta_325_use0*/shared_delta++;
    r += svc::cache::/*@sortAccount_1327_use0*/sortAccount(1, 3.5);
    r += svc::domain::flush(1);
    r += svc::config::measureAccount37(1);
    r += svc::config::collect_token(2.5);
    r += svc::cache::scanTask(1);
    svc::repo::FrozenRepositoryCustomer o2;
    r += o2.flushToken();
    svc::repo::FrozenRepositoryCustomer o3;
    r += o3.process(7u);
    r += o3.process(7u);
    return r;
}

double appRun277() {
    double r = 0;
    r += svc::storage::rotateSchedule226(1);
    svc::config::ActiveAudit o1;
    r += o1.split_payment(3.5, 1);
    r += o1.split_payment(1, 1);
    r += o1.split_payment(1, 1);
    r += svc::logging::kScale67;
    svc::repo::FrozenRepositoryCustomer o2;
    r += o2.sweep_quota(2L);
    r += o2.sweep_quota(2L);
    r += o2.weight;
    svc::config::SharedSession k3 = svc::config::SharedSession::Blue;
    r += static_cast<int>(k3);
    svc::config::/*@Cart_205_use1*/Cart<int> t4;
    r += t4.get();
    t4.set(3);
    svc::storage::/*@StableReport_1005_use0*/StableReport o5;
    r += /*@StableReport_1005_var0*/o5.splitTicket(3.5);
    r += o5.level;
    r += svc::storage::resetMapper(1, 3.5);
    svc::config::Impl::/*@shared_delta_325_use1*/shared_delta++;
    r += svc::storage::decodeOrder236(1);
    r += LOGGING_SCALE_84(2);
    r += svc::storage::Token_Default;
    svc::cache::Invoice o6;
    r += o6.visitWorker(2L);
    r += o6.rotateBalance(2L);
    r += o6.rotateBalance(2L);
    r += svc::domain::/*@sweep_queue_547_use0*/sweep_queue(2.5);
    r += svc::logging::update(2L);
    r += svc::config::Impl::/*@shared_delta_325_use2*/shared_delta;
    svc::config::ActiveAudit o7;
    r += o7.run(2L);
    r += o7.count_task(1);
    r += o7.split_payment(3.5, 1);
    r += CONFIG_SCALE_54(2);
    svc::domain::Registry k8 = svc::domain::Registry::Default;
    r += static_cast<int>(k8);
    svc::repo::FastProduct o9;
    r += o9.markInvoice(7u);
    r += o9.ratio;
    r += o9.splitSchedule(7u);
    r += svc::config::merge_address(1);
    r += svc::storage::kCap205;
    svc::logging::Notice d10;
    svc::logging::Route* b11 = &d10;
    r += b11->push(1, 1);
    return r;
}

