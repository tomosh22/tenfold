#pragma once
#include "model/sampled_audit.h"

namespace mx::model {

class SampledAudit;

class /*@SmoothMapper_1020_def0*/SmoothMapper : public mx::model::SampledAudit {
public:
    int level = 0;
    long speed = 0;
    mx::model::SampledAudit* link236 = nullptr;
    bool /*@shiftRule_1021_def0*/shiftRule(int p0, int p1) const override {
        double r = 0;
        r += p0;
        r += p1;
        r += /*@shiftRule_1021_use0*/shiftRule(1, 1);
        r += /*@shiftRule_1021_use1*/shiftRule(1, 1);
        return (bool)(r);
    }
    unsigned resolveCustomer(double p0, int p1) override;
    bool bindRule(unsigned p0) const;
    bool selectCustomer(int p0, int p1);
};

}  // namespace mx::model
