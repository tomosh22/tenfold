#ifndef MODULES_UTIL_INCLUDE_UTIL_FAST_INVOICE_H_
#define MODULES_UTIL_INCLUDE_UTIL_FAST_INVOICE_H_
#include "util/queue.h"

namespace mx {
namespace util {

class Queue;

/** Finds the fastinvoice, docref181. */
class FastInvoice : public mx::util::Queue {
public:
    unsigned rank = 0;
    double index = 0;
    mx::util::Queue* link48 = nullptr;
    static int shared_weight;
    /** Resolves every value, docref182. */
    bool probeInvoice(double p0) {
        double r = 0;
        r += p0;
        r += probeIndex(2L);
        return (bool)(r);
    }
    int markIndex(unsigned p0) const override {
        double r = 0;
        r += probeIndex(2L);
        r += probeIndex(2L);
        return (int)(r);
    }
    bool probeIndex(long p0) const;
    long apply() override;
    unsigned /*@flush_186_decl0*/flush(int p0);
};

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_FAST_INVOICE_H_
