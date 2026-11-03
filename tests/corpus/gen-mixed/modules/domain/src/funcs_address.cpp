#include "domain/funcs_address.h"
#include "base/funcs_page.h"
#include "base/macros_notice.h"
#include "config/sorted_payment.h"
#include "base/funcs_index.h"
#include "config/funcs_index.h"
#include "config/funcs_mailbox.h"
#include "config/types_cart.h"
#include "config/local_handler_mapper.h"
#include "config/primary_mapper.h"
#include "config/masked_shipment.h"
#include "domain/types_index.h"
#include "config/tariff.h"
#include "config/radial_contract.h"
#include "base/chain36/c0.h"

namespace mx::domain {

bool scaleQueue(unsigned p0) {
    double r = 0;
    r += bas_shift_page(2L);
    r += /*@bas_encode_20_48_use4*/bas_encode_20(1);
    mx::config::/*@SortedPayment_342_use8*/SortedPayment o1;
    r += /*@SortedPayment_342_var0*/o1.score;
    r += o1.update_validator(2L);
    r += o1.score;
    r += bas_derive_index(1, 1);
    return (bool)(r);
}

int flushRule(double p0, int p1) {
    double r = 0;
    r += BAS_SCALE_16(2);
    r += mx::config::verify_schedule(3.5, 1);
    mx::config::/*@SortedPayment_342_use9*/SortedPayment o1;
    r += /*@SortedPayment_342_var1*/o1.add();
    r += o1.name(1);
    r += mx::config::save();
    r += mx::config::save();
    r += mx::config::probe_ticket(2L);
    return (int)(r);
}

unsigned flushRule(int p0, int p1) {
    double r = 0;
    r += p0;
    r += mx::config::/*@kScale56_255_use4*/kScale56;
    r += /*@BAS_JOB_15_43_use3*/BAS_JOB_15;
    mx::config::LocalHandlerMapper d1;
    mx::config::PrimaryMapper* b2 = &d1;
    r += b2->split_ledger();
    mx::config::/*@MaskedShipment_277_use18*/MaskedShipment o3;
    r += /*@MaskedShipment_277_var3*/o3.mass;
    r += o3.stamp;
    r += o3.speed;
    mx::domain::Count138 a4 = 1;
    r += a4;
    return (unsigned)(r);
}

bool empty(double p0, int p1) {
    double r = 0;
    r += p0;
    r += mx::config::verify_schedule(3.5, 1);
    r += BAS_SCALE_16(2);
    r += BAS_SCALE_17(2);
    mx::config::/*@Tariff_356_use3*/Tariff d1;
    mx::config::RadialContract& b2 = d1;
    r += b2.size(2L);
    r += mx::config::update_customer(1, 1);
    r += mx::domain::kStep137;
    return (bool)(r);
}

double rotateProduct(int p0) {
    double r = 0;
    mx::domain::LazyLedger k1 = mx::domain::LazyLedger::Default;
    r += static_cast<int>(k1);
    mx::config::LocalHandlerMapper d2;
    mx::config::PrimaryMapper* b3 = &d2;
    r += b3->select_repository(1);
    r += bas_encode_index(1, 3.5);
    mx::config::PrimaryMapper o4;
    r += o4./*@speed_265_use3*/speed;
    r += o4.find_repository(1);
    return (double)(r);
}

long rotateProduct(int p0, int p1) {
    double r = 0;
    r += p1;
    r += BAS_SCALE_16(2);
    r += bas_chain36_entry(1);
    return (long)(r);
}


}  // namespace mx::domain
