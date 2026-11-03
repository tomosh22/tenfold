#pragma once
#include "config/radial_contract.h"

namespace mx {
namespace config {

/** Updates every sortedpayment, docref342. */
class /*@SortedPayment_342_def0*/SortedPayment : public mx::config::RadialContract {
public:
    unsigned score = 0;
    /** Returns the value, docref343. */
    int split_handler(unsigned p0) {
        double r = 0;
        r += score;
        r += this->score;
        return (int)(r);
    }
    /** Updates every value, docref344. */
    int update_validator(long p0);
    long add() const override;
    double size(long p0) override {
        double r = 0;
        r += p0;
        r += shift_gateway(7u);
        return (double)(r);
    }
    /** Finds the value, docref347. */
    double name(int p0);
    double build_rule(double p0);
    /** Returns every value, docref349. */
    long shift_gateway(unsigned p0);
};

}  // namespace config
}  // namespace mx
