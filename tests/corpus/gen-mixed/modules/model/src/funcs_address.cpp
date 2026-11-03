#include "model/funcs_address.h"
#include "model/types_invoice.h"
#include "model/job_worker.h"
#include "model/packed_catalog_tariff.h"

namespace mx::model {

double filterMailbox(unsigned p0) {
    double r = 0;
    r += p0;
    r += mx::model::kCap229;
    mx::model::JobWorker o1;
    r += o1.encodeTask252(3.5, 1);
    r += o1.encodeTask252(3.5, 1);
    mx::model::PackedCatalogTariff o2;
    r += o2.speed;
    r += o2.get(7u);
    r += o2.speed;
    return (double)(r);
}


}  // namespace mx::model
