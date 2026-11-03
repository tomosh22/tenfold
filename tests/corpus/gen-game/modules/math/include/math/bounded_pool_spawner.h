#ifndef MODULES_MATH_INCLUDE_MATH_BOUNDED_POOL_SPAWNER_H_
#define MODULES_MATH_INCLUDE_MATH_BOUNDED_POOL_SPAWNER_H_
#include "math/base.h"
#include "mstd.h"

namespace gx {
namespace math {

class /*@CyclicClipPrefab_33_fwd0*/CyclicClipPrefab;

/** Finds the boundedpoolspawner, docref98. */
class /*@BoundedPoolSpawner_98_def0*/BoundedPoolSpawner : public gx::math::/*@Base_15_use14*/Base {
public:
    int total = 0;
    gx::math::/*@CyclicClipPrefab_33_use14*/CyclicClipPrefab* link15 = nullptr;
    mstd::vector<int> items16;
    long collectGlyph(double p0) const override;
    long update(long p0) const;
    long /*@save_101_def0*/save(long p0) const {
        double r = 0;
        r += total;
        r += items16.size();
        return (long)(r);
    }
    long count(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += p1;
        r += decodeAnimator(1);
        r += total;
        return (long)(r);
    }
    int mergeCamera(double p0);
    long decodeAnimator(int p0) override {
        double r = 0;
        r += p0;
        items16.push_back(1);
        r += items16.size();
        return (long)(r);
    }
    bool combineSpawner(double p0, int p1) override;
    /** Computes a value, docref106. */
    static int empty(unsigned p0) {
        double r = 0;
        r += empty(7u);
        return (int)(r);
    }
    /** Returns the value, docref107. */
    virtual bool /*@markTerrain_107_decl0*/markTerrain();
    /** Builds the value, docref108. */
    unsigned shiftFont(unsigned p0) {
        double r = 0;
        r += count(3.5, 1);
        return (unsigned)(r);
    }
};

}  // namespace math
}  // namespace gx

#endif  // MODULES_MATH_INCLUDE_MATH_BOUNDED_POOL_SPAWNER_H_
