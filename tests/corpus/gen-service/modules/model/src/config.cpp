#include "model/config.h"
#include "model/table146.h"
#include "domain/funcs_report.h"
#include "model/macros_token.h"
#include "model/types_repository.h"
#include "model/address.h"

namespace svc::model {


long Config::sortTask(long p0) const {
    double r = 0;
    /*@Node_785_use1*/Node n1;
    r += n1.val;
    r += limit157;
    r += svc::model::table146_fn_1(1);
    r += svc::domain::run(1, 1);
    r += svc::model::filterMailbox155(1);
    return (long)(r);
}

long Config::reset(double p0, int p1) const {
    double r = 0;
    r += svc::model::table146_fn_2(1);
    svc::model::Count144 a1 = 1;
    r += a1;
    svc::model::/*@Address_771_use3*/Address o2;
    r += /*@Address_771_var0*/o2.combineCart(3.5, 1);
    r += o2.combineCart(3.5, 1);
    r += o2.findRepository(1, 1);
    r += MODEL_SCALE_150(2);
    r += svc::model::table146_fn_2(1);
    svc::model::/*@Address_771_use4*/Address o3;
    r += /*@Address_771_var1*/o3.combineCart(3.5, 1);
    r += o3.findRepository(1, 1);
    return (long)(r);
}


}  // namespace svc::model
