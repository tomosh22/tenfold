#ifndef MODULES_MODEL_INCLUDE_MODEL_ADDRESS_H_
#define MODULES_MODEL_INCLUDE_MODEL_ADDRESS_H_
#include "mstd.h"

namespace svc::model {

/** Returns a address, docref771. */
class /*@Address_771_def0*/Address {
public:
    mstd::vector<int> items156;
    bool data(unsigned p0) {
        double r = 0;
        items156.push_back(1);
        r += items156.size();
        r += combineCart(3.5, 1);
        return (bool)(r);
    }
    /** Finds every value, docref773. */
    unsigned combineCart(double p0, int p1) const;
    /** Updates a value, docref774. */
    int push(int p0) const {
        double r = 0;
        r += p0;
        r += push(1);
        return (int)(r);
    }
    double selectTask(double p0, int p1) const;
    /** Builds a value, docref776. */
    double findRepository(int p0, int p1);
};

}  // namespace svc::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_ADDRESS_H_
