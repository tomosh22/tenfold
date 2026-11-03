#include "config/funcs_index.h"
#include "util/queue.h"
#include "config/types_cart.h"
#include "util/macros_endpoint.h"
#include "config/primary_mapper.h"
#include "base/types_cart.h"
#include "util/funcs_repository.h"

namespace mx {
namespace config {

static int /*@scale_270_def0*/scale(int p0) { return p0 + 9; }

long verify_schedule(double p0, int p1) {
    double r = 0;
    r += p1;
    mx::util::Queue o1;
    r += mx::util::Queue::deriveInvoice(1, 3.5);
    r += /*@scale_270_use0*/scale(1);
    r += /*@scale_270_use1*/scale(1);
    r += mx::config::/*@kScale56_255_use0*/kScale56;
    r += /*@UTIL_TARIFF_41_165_use1*/UTIL_TARIFF_41;
    return (long)(r);
}

bool save() {
    double r = 0;
    mx::config::PrimaryMapper o1;
    r += o1.visit_customer(2L);
    r += o1.select_repository(1);
    r += o1.find_repository(1);
    mx::config::PrimaryMapper o2;
    r += mx::config::PrimaryMapper::/*@reset_contract_261_use0*/reset_contract(7u);
    r += mx::config::PrimaryMapper::/*@reset_contract_261_use1*/reset_contract(7u);
    r += BAS_NONE_3;
    r += mx::util::data(2L);
    mx::config::PrimaryMapper o3;
    r += o3.index;
    r += o3.index;
    return (bool)(r);
}


}  // namespace config
}  // namespace mx
