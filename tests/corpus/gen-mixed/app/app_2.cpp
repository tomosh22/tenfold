#include "app.h"
#include "domain/domain.h"
#include "logging/logging.h"
#include "model/macros_profile.h"
#include "model/packed_mailbox_policy.h"
#include "util/funcs_event.h"
#include "model/packed_catalog_tariff.h"
#include "domain/types_index.h"
#include "config/local_handler_mapper.h"
#include "util/funcs_job.h"
#include "util/token.h"
#include "util/fast_invoice.h"
#include "logging/funcs_account.h"
#include "config/funcs_mailbox.h"
#include "config/types_cart.h"
#include "domain/scoped_job.h"
#include "logging/balance2.h"
#include "model/active_task.h"
#include "util/funcs_profile.h"
#include "domain/remote_address.h"
#include "config/masked_shipment.h"
#include "util/queue.h"
#include "model/types_tenant.h"
#include "config/funcs_index.h"
#include "logging/macros_token.h"
#include "util/macros_endpoint.h"
#include "util/types_product.h"
#include "model/funcs_endpoint.h"
#include "util/types_mailbox.h"
#include "domain/binned_quota_repository.h"

double appRun261() {
    double r = 0;
    r += MODEL_PAYMENT_243;
    mx::model::/*@PackedMailboxPolicy_1050_use3*/PackedMailboxPolicy<int> t1;
    r += t1.get();
    t1.set(3);
    r += mx::util::next(3.5);
    mx::model::PackedCatalogTariff o2;
    r += o2.offset;
    mx::domain::LazyLedger k3 = mx::domain::LazyLedger::Green;
    r += static_cast<int>(k3);
    mx::config::LocalHandlerMapper d4;
    mx::config::LocalHandlerMapper& b5 = d4;
    r += b5.select_repository(1);
    mx::model::PackedCatalogTariff o6;
    r += o6.offset;
    r += o6.speed;
    r += o6.speed;
    r += mx::util::value();
    mx::util::Token o7;
    r += o7./*@rotateAudit_171_use0*/rotateAudit(1);
    r += mx::util::FastInvoice::shared_weight;
    r += mx::logging::reset_route(3.5, 1);
    r += mx::config::find_audit(3.5);
    r += mx::config::kBase55;
    mx::domain::ScopedJob o8;
    r += o8.height;
    r += o8.splitToken(3.5);
    r += o8.init(1);
    mx::model::PackedCatalogTariff o9;
    r += o9.speed;
    r += o9.offset;
    r += o9.speed;
    mx::logging::Balance2 o10;
    r += o10.markIndex(7u);
    r += o10.empty(1, 3.5);
    r += o10.empty(1, 3.5);
    mx::model::ActiveTask d11;
    mx::model::ActiveTask* b12 = &d11;
    r += b12->sortSession();
    return r;
}

double appRun262() {
    double r = 0;
    r += mx::model::decodeLedger249(1);
    r += mx::util::saveRoute(2L);
    mx::domain::/*@RemoteAddress_688_use2*/RemoteAddress d1;
    mx::config::/*@MaskedShipment_277_use2*/MaskedShipment* b2 = &d1;
    r += b2->compute_account(3.5, 1);
    r += mx::model::countAccount250(1);
    mx::util::FastInvoice d3;
    mx::util::FastInvoice* b4 = &d3;
    r += b4->count(1, 1);
    mx::model::/*@PackedMailboxPolicy_1050_use4*/PackedMailboxPolicy<int> t5;
    r += t5.get();
    t5.set(3);
    mx::util::Queue o6;
    r += o6.run(2L);
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += mx::model::kLimit238;
    r += mx::config::verify_schedule(3.5, 1);
    r += LOGGING_CUSTOMER_116;
    r += UTIL_SCALE_43(2);
    r += mx::model::/*@markPayment_1054_use2*/markPayment<int>(2);
    r += mx::domain::ScopedJob::/*@shared_weight_684_use0*/shared_weight;
    r += mx::util::/*@kScale37_134_use0*/kScale37;
    r += mx::logging::saveNotice121(1);
    r += mx::model::id(7u);
    r += mx::util::kLimit52;
    mx::domain::/*@BinnedQuotaRepository_697_use0*/BinnedQuotaRepository o7;
    r += /*@BinnedQuotaRepository_697_var0*/o7.size(1, 3.5);
    r += o7.clear(2L);
    return r;
}

