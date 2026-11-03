#ifndef MODULES_MODEL_INCLUDE_MODEL_SAMPLED_AUDIT_H_
#define MODULES_MODEL_INCLUDE_MODEL_SAMPLED_AUDIT_H_
#include "model/product_route.h"

namespace mx::model {

class SortedValidator;

class SampledAudit : public mx::model::ProductRoute {
public:
    double weight = 0;
    unsigned stamp = 0;
    bool width = 0;
    mx::model::SortedValidator* link235 = nullptr;
    static int shared_stamp;
    unsigned resolveCustomer(double p0, int p1) override;
    /** Updates a value, docref1011. */
    int next(double p0);
    bool /*@shiftRule_1012_decl0*/shiftRule(int p0, int p1) const override;
    bool encodeTask();
    /** Returns the value, docref1014. */
    long resolveLedger(int p0);
};

}  // namespace mx::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_SAMPLED_AUDIT_H_
