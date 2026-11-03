#pragma once
#include "config/sorted_payment.h"

namespace mx {
namespace config {

/** Updates a tariff, docref356. */
class /*@Tariff_356_def0*/Tariff : public mx::config::/*@SortedPayment_342_use1*/SortedPayment {
public:
    double size(long p0) override;
    bool /*@mark_mailbox_358_def0*/mark_mailbox(int p0, double p1) const override {
        double r = 0;
        r += p0;
        r += p1;
        r += merge_handler(3.5, 1);
        r += merge_handler(3.5, 1);
        return (bool)(r);
    }
    /** Resolves every value, docref359. */
    bool merge_handler(double p0, int p1) const;
    /** Returns the value, docref360. */
    double process(long p0);
};

}  // namespace config
}  // namespace mx
