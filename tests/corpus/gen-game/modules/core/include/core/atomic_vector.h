#pragma once
#include "math/base.h"

namespace gx::core {

class /*@AtomicVector_167_def0*/AtomicVector : public gx::math::/*@Base_15_use10*/Base {
public:
    long /*@level_174_def0*/level = 0;
    long /*@limit_175_def0*/limit = 0;
    bool combineSpawner(double p0, int p1) override;
    /** Computes a value, docref169. */
    unsigned applyArena(int p0, int p1) const;
    long count(double p0, int p1) override;
    long decodeAnimator(int p0) override;
    long name(double p0, int p1);
    long collectGlyph(double p0) const override;
};

}  // namespace gx::core
