#pragma once
#include "container/animator_agent.h"

namespace gx::container {

class /*@BindingSkeleton_592_def0*/BindingSkeleton : public gx::container::/*@AnimatorAgent_543_use1*/AnimatorAgent {
public:
    BindingSkeleton() {}
    explicit BindingSkeleton(int) {}
    long decodeAnimator(int p0) override;
    long count(double p0, int p1) override;
    bool combineSpawner(double p0, int p1) override;
    /** Returns every value, docref596. */
    unsigned value();
    long collectGlyph(double p0) const override;
    bool /*@markTerrain_598_def0*/markTerrain() override {
        double r = 0;
        r += collectGlyph(3.5);
        r += combineSpawner(3.5, 1);
        return (bool)(r);
    }
    unsigned flushBitmap(int p0) const;
    /** Returns the value, docref600. */
    virtual double get(double p0) const {
        double r = 0;
        r += p0;
        r += collectGlyph(3.5);
        r += collectGlyph(3.5);
        return (double)(r);
    }
};

}  // namespace gx::container
