#include "model/packed_catalog_tariff.h"
#include "list/macros_schedule.h"
#include "model/types_notice.h"
#include "list/funcs_socket.h"

namespace mx::model {


int PackedCatalogTariff::fetchMapper(unsigned p0) const {
    double r = 0;
    r += p0;
    r += lis_bind_166(1);
    r += mx::model::kStep225;
    r += /*@lis_apply_socket_887_use2*/lis_apply_socket(3.5, 1);
    return (int)(r);
}


}  // namespace mx::model
