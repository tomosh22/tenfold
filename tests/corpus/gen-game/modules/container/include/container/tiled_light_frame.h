#pragma once
#include "math/base.h"

namespace gx::container {

class /*@TiledLightFrame_511_def0*/TiledLightFrame : public gx::math::/*@Base_15_use6*/Base {
public:
    int rank = 0;
    static int /*@shared_scale_520_decl0*/shared_scale;
    /** Finds a value, docref512. */
    bool applyShader(double p0) const {
        double r = 0;
        r += p0;
        r += rank;
        return (bool)(r);
    }
    bool resetTrigger(int p0, double p1) {
        double r = 0;
        r += combineSpawner(3.5, 1);
        return (bool)(r);
    }
    /** Builds a value, docref514. */
    bool save(long p0);
    long decodeAnimator(int p0) override {
        double r = 0;
        r += save(2L);
        r += applyShader(3.5);
        return (long)(r);
    }
    bool combineSpawner(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += rank;
        return (bool)(r);
    }
    long /*@collectGlyph_517_decl0*/collectGlyph(double p0) const override;
    long count(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += p1;
        r += rank;
        return (long)(r);
    }
};

}  // namespace gx::container
