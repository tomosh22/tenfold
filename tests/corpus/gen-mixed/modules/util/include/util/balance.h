#ifndef MODULES_UTIL_INCLUDE_UTIL_BALANCE_H_
#define MODULES_UTIL_INCLUDE_UTIL_BALANCE_H_
#include "util/basic_order.h"

namespace mx {
namespace util {

/** Finds every balance, docref229. */
class Balance : public mx::util::BasicOrder {
public:
    int depth = 0;
    int mergeAccount(int p0) {
        double r = 0;
        r += p0;
        r += reset(1, 1);
        r += reset(1, 1);
        return (int)(r);
    }
    bool count(int p0, int p1) const override;
    double reset(int p0, int p1) {
        double r = 0;
        r += p1;
        r += count(1, 1);
        r += this->depth;
        return (double)(r);
    }
    long apply() override {
        double r = 0;
        r += depth;
        r += depth;
        return (long)(r);
    }
    double empty(int p0, double p1) override {
        double r = 0;
        r += this->depth;
        return (double)(r);
    }
};

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_BALANCE_H_
