#ifndef MODULES_MATH_INCLUDE_MATH_RADIAL_JOINT_H_
#define MODULES_MATH_INCLUDE_MATH_RADIAL_JOINT_H_
#include "math/base.h"

namespace gx {
namespace math {

class /*@RadialJoint_42_def0*/RadialJoint : public gx::math::/*@Base_15_use17*/Base {
public:
    unsigned ratio = 0;
    long width = 0;
    unsigned ratio7 = 0;
    long scanFont(double p0, int p1) const;
    /** Updates a value, docref44. */
    long loadLight(int p0, int p1);
    long decodeAnimator(int p0) override;
    /** Updates a value, docref46. */
    unsigned load(unsigned p0);
    long count(double p0, int p1) override;
    double splitBatch(double p0) const;
    /** Finds a value, docref49. */
    double countSpawner(int p0);
    long collectGlyph(double p0) const override;
    bool combineSpawner(double p0, int p1) override {
        double r = 0;
        r += p1;
        r += this->ratio7;
        return (bool)(r);
    }
};

}  // namespace math
}  // namespace gx

#endif  // MODULES_MATH_INCLUDE_MATH_RADIAL_JOINT_H_
