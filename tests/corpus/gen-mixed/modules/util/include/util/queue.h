#ifndef MODULES_UTIL_INCLUDE_UTIL_QUEUE_H_
#define MODULES_UTIL_INCLUDE_UTIL_QUEUE_H_
#include "util/basic_order.h"

namespace mx {
namespace util {

/** Finds the queue, docref154. */
class Queue : public mx::util::BasicOrder {
public:
    double empty(int p0, double p1) override;
    bool next() {
        double r = 0;
        r += count(1, 1);
        return (bool)(r);
    }
    /** Resolves a value, docref157. */
    static unsigned deriveInvoice(int p0, double p1);
    bool count(int p0, int p1) const override {
        double r = 0;
        r += p1;
        r += markIndex(7u);
        r += run(2L);
        return (bool)(r);
    }
    static unsigned update(int p0, double p1) {
        double r = 0;
        r += p1;
        r += deriveInvoice(1, 3.5);
        r += update(1, 3.5);
        return (unsigned)(r);
    }
    int run(long p0) const {
        double r = 0;
        r += p0;
        r += update(1, 3.5);
        return (int)(r);
    }
    virtual int markIndex(unsigned p0) const;
    long apply() override;
};

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_QUEUE_H_
