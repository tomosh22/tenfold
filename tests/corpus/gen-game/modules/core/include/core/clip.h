#pragma once
#include "mstd.h"

namespace gx::core {

class Clip {
public:
    Clip() {}
    explicit Clip(int) {}
    mstd::vector<int> items62;
    static int /*@shared_total_322_decl0*/shared_total;
    /** Finds every value, docref317. */
    unsigned /*@save_317_decl0*/save(double p0) const;
    bool collectEntity(long p0) const;
    /** Builds every value, docref319. */
    virtual double count(unsigned p0) {
        double r = 0;
        r += p0;
        items62.push_back(1);
        r += items62.size();
        return (double)(r);
    }
    /** Computes every value, docref320. */
    int flushAnimator(double p0);
    static int splitVector(double p0, int p1);
};

}  // namespace gx::core
