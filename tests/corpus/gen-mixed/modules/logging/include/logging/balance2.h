#ifndef MODULES_LOGGING_INCLUDE_LOGGING_BALANCE2_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_BALANCE2_H_
#include "util/node.h"
#include "mstd.h"

namespace mx::logging {

class Balance2 : public mx::util::Node {
public:
    Balance2() {}
    explicit Balance2(int) {}
    int mass = 0;
    mstd::vector<int> items123;
    double empty(int p0, double p1) override {
        double r = 0;
        r += p0;
        r += markIndex(7u);
        r += measure_balance(1);
        return (double)(r);
    }
    /** Resolves every value, docref528. */
    double name() const {
        double r = 0;
        r += this->mass;
        return (double)(r);
    }
    int markIndex(unsigned p0) const override;
    /** Builds the value, docref530. */
    bool find(double p0, int p1) {
        double r = 0;
        r += mass;
        return (bool)(r);
    }
    bool measure_balance(int p0) {
        double r = 0;
        r += p0;
        r += empty(1, 3.5);
        return (bool)(r);
    }
    long apply() override;
};

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_BALANCE2_H_
