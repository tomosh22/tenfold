#include "model/job.h"
#include "model/sampled_audit.h"
#include "model/macros_profile.h"
#include "model/packed_catalog_tariff.h"
#include "model/sorted_validator.h"
#include "model/funcs_validator.h"
#include "model/types_notice.h"
#include "model/packed_mailbox_policy.h"
#include "model/chain237/c0.h"

namespace mx::model {


bool /*@Job_1081_use2*/Job::verifyTariff() const {
    double r = 0;
    mx::model::SampledAudit::shared_stamp++;
    r += MODEL_SCALE_246(2);
    mx::model::PackedCatalogTariff o1;
    r += o1.offset;
    r += o1.get(7u);
    r += o1.get(7u);
    mx::model::SortedValidator o2;
    r += o2.loadTicket();
    r += mx::model::/*@verifyCustomer_1006_use1*/verifyCustomer(1, 3.5);
    r += mx::model::kStep225;
    return (bool)(r);
}

double /*@Job_1081_use3*/Job::size() {
    double r = 0;
    mx::model::PackedCatalogTariff o1;
    r += o1.offset;
    r += mx::model::/*@encodeSession_1055_use4*/encodeSession(2.5);
    r += mx::model::chain237_l0(1);
    return (double)(r);
}


}  // namespace mx::model
