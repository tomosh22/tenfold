#include "domain/funcs_event.h"
#include "config/tariff.h"
#include "config/sorted_payment.h"
#include "config/local_handler_mapper.h"
#include "config/funcs_mailbox.h"
#include "config/funcs_index.h"
#include "domain/funcs_address.h"
#include "base/macros_notice.h"
#include "base/types_report.h"

namespace mx::domain {

double sweepIndex(double p0) {
    double r = 0;
    mx::config::/*@Tariff_356_use4*/Tariff d1;
    mx::config::/*@SortedPayment_342_use10*/SortedPayment& b2 = d1;
    r += b2./*@mark_mailbox_334_use2*/mark_mailbox(1, 3.5);
    mx::config::LocalHandlerMapper d3;
    mx::config::LocalHandlerMapper* b4 = &d3;
    r += b4->split_ledger();
    return (double)(r);
}

bool findShipment(int p0, int p1) {
    double r = 0;
    r += p1;
    r += mx::config::update_customer(1, 3.5);
    r += mx::config::verify_schedule(3.5, 1);
    mx::config::LocalHandlerMapper d1;
    mx::config::LocalHandlerMapper& b2 = d1;
    r += b2.select_repository(1);
    r += mx::domain::rotateProduct(1, 1);
    return (bool)(r);
}

bool rotateMailbox() {
    double r = 0;
    r += mx::config::LocalHandlerMapper::shared_count;
    r += /*@BAS_JOB_15_43_use4*/BAS_JOB_15;
    r += mx::domain::scaleQueue(7u);
    r += bas_limit_26;
    return (bool)(r);
}


}  // namespace mx::domain
