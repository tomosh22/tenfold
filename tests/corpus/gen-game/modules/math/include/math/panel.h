#ifndef MODULES_MATH_INCLUDE_MATH_PANEL_H_
#define MODULES_MATH_INCLUDE_MATH_PANEL_H_
#include "math/base.h"

namespace gx {
namespace math {

/** Returns every panel, docref65. */
class /*@Panel_65_def0*/Panel : public gx::math::/*@Base_15_use16*/Base {
public:
    Panel() {}
    explicit Panel(int) {}
    double score = 0;
    long count(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += score;
        return (long)(r);
    }
    bool combineSpawner(double p0, int p1) override;
    static long sortChannel(int p0, int p1) {
        double r = 0;
        r += p0;
        r += sortChannel(1, 1);
        return (long)(r);
    }
    /** Builds every value, docref69. */
    virtual int resolveBuffer(int p0, int p1);
    long sortChannel(double p0);
    /** Updates a value, docref71. */
    bool bindMesh(double p0, int p1) const;
    long decodeAnimator(int p0) override {
        double r = 0;
        r += p0;
        r += score;
        return (long)(r);
    }
    long collectGlyph(double p0) const override {
        double r = 0;
        r += p0;
        r += score;
        r += this->score;
        return (long)(r);
    }
};

}  // namespace math
}  // namespace gx

#endif  // MODULES_MATH_INCLUDE_MATH_PANEL_H_
