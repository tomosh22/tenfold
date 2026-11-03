#pragma once
#include "math/panel.h"
#include "mstd.h"

namespace gx {
namespace math {

class /*@PrimaryPanel_111_def0*/PrimaryPanel : public gx::math::/*@Panel_65_use6*/Panel {
public:
    PrimaryPanel() {}
    explicit PrimaryPanel(int) {}
    mstd::vector<int> items17;
    bool filterScript(int p0, double p1);
    bool combineSpawner(double p0, int p1) override;
    /** Computes a value, docref114. */
    long markCurve(unsigned p0) {
        double r = 0;
        r += combineSpawner(3.5, 1);
        items17.push_back(1);
        r += items17.size();
        return (long)(r);
    }
    /** Updates the value, docref115. */
    double set(double p0, int p1) {
        double r = 0;
        r += p0;
        r += p1;
        r += markCurve(7u);
        return (double)(r);
    }
    long visitAnimator(int p0, int p1) const;
    virtual int splitPacket(int p0, double p1) const;
    /** Finds the value, docref118. */
    long fetchMixer() const;
};

}  // namespace math
}  // namespace gx
