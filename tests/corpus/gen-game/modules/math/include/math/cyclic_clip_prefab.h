#ifndef MODULES_MATH_INCLUDE_MATH_CYCLIC_CLIP_PREFAB_H_
#define MODULES_MATH_INCLUDE_MATH_CYCLIC_CLIP_PREFAB_H_
#include "math/base.h"

namespace gx {
namespace math {

class /*@CyclicClipPrefab_33_def0*/CyclicClipPrefab : public gx::math::/*@Base_15_use15*/Base {
public:
    bool combineSpawner(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += p1;
        r += count(3.5, 1);
        return (bool)(r);
    }
    long decodeAnimator(int p0) override;
    long collectGlyph(double p0) const override {
        double r = 0;
        r += p0;
        r += name(1, 1);
        r += name(1, 1);
        return (long)(r);
    }
    /** Returns every value, docref37. */
    int mergeBody(int p0, int p1);
    long encodeVector(double p0);
    /** Computes every value, docref39. */
    double selectLight();
    static bool name(int p0, int p1);
    long count(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += p1;
        r += decodeAnimator(1);
        r += decodeAnimator(1);
        return (long)(r);
    }
};

}  // namespace math
}  // namespace gx

#endif  // MODULES_MATH_INCLUDE_MATH_CYCLIC_CLIP_PREFAB_H_
