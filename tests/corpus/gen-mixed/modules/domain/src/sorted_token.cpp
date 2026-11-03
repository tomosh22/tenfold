#include "domain/sorted_token.h"
#include "domain/remote_address.h"
#include "base/types_cart.h"
#include "domain/funcs_address.h"
#include "domain/types_shipment.h"
#include "domain/scoped_job.h"
#include "domain/funcs_event.h"
#include "base/macros_notice.h"
#include "config/local_handler_mapper.h"
#include "config/primary_mapper.h"
#include "config/sorted_payment.h"
#include "config/masked_shipment.h"
#include "domain/types_index.h"

namespace mx::domain {


int /*@SortedToken_702_use4*/SortedToken::/*@markEndpoint_703_def0*/markEndpoint(int p0, double p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += /*@markEndpoint_703_use1*/markEndpoint(1, 3.5);
    mx::domain::/*@RemoteAddress_688_use13*/RemoteAddress o1;
    r += o1.score;
    r += o1.top(2L);
    r += o1.top(2L);
    r += /*@bas_step_11_12_use1*/bas_step_11;
    r += /*@markEndpoint_703_use2*/markEndpoint(1, 3.5);
    return (int)(r);
}

bool /*@SortedToken_702_use5*/SortedToken::shift_task(double p0, int p1) {
    double r = 0;
    r += p1;
    r += mx::domain::flushRule(3.5, 1);
    r += mx::domain::Provider_Auto;
    return (bool)(r);
}

double /*@SortedToken_702_use6*/SortedToken::compute_account(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += mx::domain::scaleQueue(7u);
    r += mx::domain::scaleQueue(7u);
    return (double)(r);
}

double /*@SortedToken_702_use7*/SortedToken::save(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += mx::domain::ScopedJob::/*@shared_weight_684_use5*/shared_weight;
    mx::domain::ScopedJob o1;
    r += o1.splitToken(3.5);
    r += mx::domain::mergeJob(1, 3.5);
    r += bas_compute_19(1);
    mx::config::LocalHandlerMapper d2;
    mx::config::PrimaryMapper& b3 = d2;
    r += b3.split_ledger();
    return (double)(r);
}

int /*@SortedToken_702_use8*/SortedToken::deriveRepository(double p0) const {
    double r = 0;
    mx::config::/*@SortedPayment_342_use12*/SortedPayment d1;
    mx::config::/*@SortedPayment_342_use13*/SortedPayment& b2 = d1;
    r += b2.add();
    mx::domain::/*@RemoteAddress_688_use14*/RemoteAddress d3;
    mx::config::/*@MaskedShipment_277_use20*/MaskedShipment* b4 = &d3;
    r += b4->compute_account(3.5, 1);
    r += /*@markEndpoint_703_use3*/markEndpoint(1, 3.5);
    mx::domain::Count142 a5 = 1;
    r += a5;
    r += mx::domain::kStep137;
    r += mx::domain::kCap140;
    return (int)(r);
}

int /*@SortedToken_702_use9*/SortedToken::rotatePolicy(int p0) {
    double r = 0;
    r += p0;
    mx::domain::/*@RemoteAddress_688_use15*/RemoteAddress o1;
    r += o1.probeCustomer(3.5);
    r += o1.top(2L);
    r += o1.speed143;
    r += mx::domain::ScopedJob::/*@shared_weight_684_use6*/shared_weight;
    r += /*@BAS_JOB_15_43_use5*/BAS_JOB_15;
    return (int)(r);
}


}  // namespace mx::domain
