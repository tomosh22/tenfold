#pragma once
#include "math/base.h"

namespace gx::asset {

class Context;

/** Builds a mergedlightpanel, docref1026. */
class /*@MergedLightPanel_1026_def0*/MergedLightPanel : public gx::math::/*@Base_15_use3*/Base {
public:
    int phase = 0;
    int /*@count157_1034_def0*/count157 = 0;
    gx::asset::Context* link158 = nullptr;
    bool combineSpawner(double p0, int p1) override;
    long collectGlyph(double p0) const override;
    unsigned value(double p0, int p1) const;
    long count(double p0, int p1) override;
    long decodeAnimator(int p0) override {
        double r = 0;
        r += p0;
        r += /*@count157_1034_use0*/count157;
        return (long)(r);
    }
    /** Computes the value, docref1032. */
    unsigned value(int p0) const {
        double r = 0;
        r += p0;
        r += collectGlyph(3.5);
        r += value(1);
        return (unsigned)(r);
    }
};

}  // namespace gx::asset
