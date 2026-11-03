#include "app.h"
#include "config/config.h"
#include "model/model.h"
#include "domain/scoped_job.h"
#include "list/funcs_cache.h"
#include "config/masked_shipment.h"
#include "list/types_token.h"
#include "model/packed_mailbox_policy.h"
#include "list/macros_schedule.h"
#include "util/queue.h"
#include "config/funcs_index.h"
#include "model/types_tenant.h"
#include "config/types_cart.h"
#include "model/funcs_endpoint.h"
#include "util/funcs_job.h"
#include "logging/types_validator.h"
#include "domain/types_shipment.h"
#include "logging/balance2.h"
#include "util/node.h"
#include "domain/remote_address.h"
#include "config/linear_product.h"
#include "model/packed_catalog_tariff.h"
#include "config/registry.h"
#include "list/funcs_socket.h"
#include "model/config.h"
#include "domain/binned_quota_repository.h"
#include "config/local_handler_mapper.h"
#include "model/funcs_quota.h"
#include "logging/funcs_account.h"
#include "model/active_task.h"
#include "domain/types_index.h"
#include "util/macros_endpoint.h"
#include "model/funcs_worker.h"

double appRun263() {
    double r = 0;
    mx::domain::ScopedJob o1;
    r += o1.init(1);
    r += o1.count(1);
    r += o1.init(1);
    r += /*@lis_filter_cache_813_use0*/lis_filter_cache();
    mx::config::/*@MaskedShipment_277_use3*/MaskedShipment::/*@shared_speed_286_use1*/shared_speed++;
    r += lis_step_188;
    mx::model::/*@PackedMailboxPolicy_1050_use5*/PackedMailboxPolicy<int> t2;
    r += t2.get();
    t2.set(3);
    r += lis_bind_166(1);
    mx::util::Queue d3;
    mx::util::Queue* b4 = &d3;
    r += b4->apply();
    r += mx::config::save(3.5, 1);
    r += mx::model::kBase239;
    r += mx::model::/*@markPayment_1054_use3*/markPayment<int>(2);
    r += mx::config::kBase55;
    r += mx::model::top(1, 1);
    r += mx::domain::ScopedJob::/*@shared_weight_684_use1*/shared_weight;
    r += mx::util::top(1);
    mx::util::Queue d5;
    mx::util::Queue* b6 = &d5;
    r += b6->apply();
    r += mx::logging::kCap131;
    r += mx::model::/*@encodeSession_1055_use0*/encodeSession(2.5);
    mx::domain::Count142 a7 = 1;
    r += a7;
    mx::logging::Balance2 d8;
    mx::util::Node* b9 = &d8;
    r += b9->empty(1, 3.5);
    mx::domain::/*@RemoteAddress_688_use3*/RemoteAddress o10;
    r += /*@RemoteAddress_688_var2*/o10.probeCustomer(3.5);
    r += mx::domain::ScopedJob::/*@shared_weight_684_use2*/shared_weight;
    mx::config::LinearProduct o11;
    r += o11.limit;
    r += o11.level;
    mx::model::PackedCatalogTariff o12;
    r += o12.speed;
    mx::config::Registry o13;
    r += o13.load_quota(3.5);
    r += o13.rank;
    r += o13.flags;
    return r;
}

double appRun264() {
    double r = 0;
    mx::util::Queue o1;
    r += o1.run(2L);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += /*@lis_apply_socket_887_use0*/lis_apply_socket(3.5, 1);
    mx::logging::Balance2 o2;
    r += o2.mass;
    r += o2.mass;
    r += o2.find(3.5, 1);
    mx::model::Config o3;
    r += o3.height;
    r += o3.rotateTenant(7u);
    r += mx::model::Config::markMailbox(2L);
    mx::domain::/*@BinnedQuotaRepository_697_use1*/BinnedQuotaRepository o4;
    r += /*@BinnedQuotaRepository_697_var1*/o4.size(1, 3.5);
    mx::util::Queue o5;
    r += o5.markIndex(7u);
    r += o5.markIndex(7u);
    r += o5.markIndex(7u);
    mx::config::LocalHandlerMapper d6;
    mx::config::LocalHandlerMapper* b7 = &d6;
    r += b7->select_repository(1);
    r += mx::model::mergeContract(1);
    mx::domain::/*@BinnedQuotaRepository_697_use2*/BinnedQuotaRepository o8;
    r += /*@BinnedQuotaRepository_697_var2*/o8.size(1, 3.5);
    r += mx::logging::init(1, 1);
    r += mx::config::visit_report(1, 3.5);
    mx::model::ActiveTask o9;
    r += o9.resolveCustomer(3.5, 1);
    r += o9.shiftRule(1, 1);
    r += o9.mass;
    r += mx::model::id(7u);
    mx::util::Queue o10;
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += mx::domain::kStep137;
    r += /*@UTIL_TARIFF_41_165_use0*/UTIL_TARIFF_41;
    r += mx::model::/*@encodeSession_1055_use1*/encodeSession(2.5);
    r += mx::model::push(1);
    mx::config::LocalHandlerMapper o11;
    r += o11.flags;
    r += o11.select_repository(1);
    r += mx::config::visit_report(1, 3.5);
    return r;
}

