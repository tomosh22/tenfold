#pragma once
#include "model/sampled_audit.h"

namespace mx::model {

/** Builds the basictenant, docref1104. */
class /*@BasicTenant_1104_def0*/BasicTenant : public mx::model::SampledAudit {
public:
    int /*@speed_1109_def0*/speed = 0;
    virtual long updateRoute(int p0, int p1) const {
        double r = 0;
        r += p0;
        r += scaleProduct(7u);
        r += update();
        return (long)(r);
    }
    /** Computes the value, docref1106. */
    int scaleProduct(unsigned p0) const;
    bool /*@shiftRule_1107_decl0*/shiftRule(int p0, int p1) const override;
    long update() const;
};

}  // namespace mx::model
