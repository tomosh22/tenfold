#pragma once
#include "util/queue.h"
#include "mstd.h"

namespace mx {
namespace util {

class Node : public mx::util::Queue {
public:
    mstd::vector<int> items49;
    bool count(int p0, int p1) const override {
        double r = 0;
        r += p1;
        r += items49.size();
        r += count(1, 1);
        return (bool)(r);
    }
    long apply() override;
    static int combineEvent(unsigned p0);
    /** Builds the value, docref206. */
    bool countProduct(int p0, double p1) {
        double r = 0;
        r += p1;
        items49.push_back(1);
        r += items49.size();
        return (bool)(r);
    }
    /** Updates every value, docref207. */
    long scaleNotice(int p0) {
        double r = 0;
        r += scaleNotice(1);
        r += combineEvent(7u);
        return (long)(r);
    }
};

}  // namespace util
}  // namespace mx
