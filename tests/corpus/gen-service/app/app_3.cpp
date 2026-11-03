#include "app.h"
#include "logging/logging.h"
#include "config/config.h"
#include "logging/funcs_ledger.h"
#include "storage/types_payment.h"
#include "domain/nested_endpoint.h"
#include "util/static_ledger.h"
#include "storage/funcs_endpoint.h"
#include "storage/address2.h"
#include "logging/impl.h"
#include "storage/macros_schedule.h"
#include "logging/macros_account.h"
#include "domain/stable_event.h"
#include "cache/funcs_account.h"
#include "cache/invoice.h"
#include "cache/config.h"
#include "logging/registry.h"
#include "logging/types_audit.h"

double appRun281() {
    double r = 0;
    r += svc::logging::encode_tariff();
    svc::storage::Handle206 a1 = 1;
    r += a1;
    svc::domain::NestedEndpoint d2;
    svc::util::StaticLedger& b3 = d2;
    r += b3.combineCustomer(1, 1);
    r += svc::storage::combineLedger(1, 3.5);
    r += svc::storage::/*@decodeSession_1027_use0*/decodeSession<int>(2);
    svc::logging::Impl o4;
    r += o4.init(2L);
    r += o4.init(2L);
    r += o4.fetch_rule(3.5);
    r += STORAGE_TENANT_231;
    r += LOGGING_SCALE_83(2);
    r += svc::storage::resetMapper(1, 3.5);
    svc::domain::StableEvent o5;
    r += o5.level;
    r += svc::cache::bindLedger(2L);
    svc::cache::Invoice d6;
    svc::cache::Config& b7 = d6;
    r += b7.set(1, 3.5);
    svc::logging::/*@Registry_413_use3*/Registry o8;
    r += /*@Registry_413_var3*/o8.name(1);
    r += svc::logging::kLimit76;
    return r;
}

