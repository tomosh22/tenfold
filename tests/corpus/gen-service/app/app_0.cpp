#include "app.h"
#include "storage/storage.h"
#include "logging/logging.h"
#include "storage/base.h"
#include "config/entry.h"
#include "logging/funcs_balance.h"
#include "cache/macros_task.h"
#include "config/active_audit.h"
#include "storage/node.h"
#include "config/sampled_endpoint_gateway.h"
#include "repo/types_product.h"
#include "logging/types_mapper.h"
#include "logging/funcs_ledger.h"
#include "cache/invoice.h"
#include "cache/config.h"
#include "domain/types_mailbox.h"
#include "cache/types_route.h"
#include "config/cart.h"
#include "repo/sparse_repository.h"
#include "repo/fast_product.h"
#include "domain/nested_endpoint.h"
#include "util/static_ledger.h"

double appRun274() {
    double r = 0;
    svc::storage::/*@Base_1000_use0*/Base o1;
    r += /*@Base_1000_var0*/o1.findTenant(3.5, 1);
    r += o1.findTenant(3.5, 1);
    r += o1.rank;
    svc::config::Entry d2;
    svc::config::Entry* b3 = &d2;
    r += b3->verify_audit(1, 1);
    r += svc::logging::flush();
    r += svc::cache::resolveNotice252(1);
    svc::config::ActiveAudit o4;
    r += o4.add();
    r += o4.split_payment(1, 1);
    r += o4.split_payment(1, 1);
    svc::storage::/*@Node_1077_use0*/Node d5;
    svc::storage::/*@Node_1077_use1*/Node& b6 = d5;
    r += b6.verifyPayment(7u);
    svc::config::/*@SampledEndpointGateway_333_use0*/SampledEndpointGateway o7;
    r += /*@SampledEndpointGateway_333_var0*/o7.update(2L);
    r += o7.merge_payment(1);
    r += o7.merge_payment(1);
    svc::repo::Handle187 a8 = 1;
    r += a8;
    r += svc::logging::count(7u);
    r += static_cast<int>(svc::logging::/*@Handle_374_use0*/Handle::Count);
    r += svc::logging::encode_tariff();
    svc::logging::Id69 a9 = 1;
    r += a9;
    svc::cache::Invoice d10;
    svc::cache::Config& b11 = d10;
    r += b11.scanNotice(2L);
    r += static_cast<int>(svc::domain::/*@StableToken_535_use0*/StableToken::Auto);
    r += svc::cache::AtomicCustomer_Default;
    svc::config::/*@Cart_209_use0*/Cart<bool> t12;
    r += t12.get();
    t12.set(true);
    svc::cache::Invoice d13;
    svc::cache::Config* b14 = &d13;
    r += b14->scanNotice(2L);
    svc::config::/*@Cart_205_use0*/Cart<int> t15;
    r += t15.get();
    t15.set(3);
    svc::repo::SparseRepository d16;
    svc::repo::FastProduct* b17 = &d16;
    r += b17->markInvoice(7u);
    svc::domain::NestedEndpoint d18;
    svc::util::StaticLedger& b19 = d18;
    r += b19.combineCustomer(1, 1);
    return r;
}

