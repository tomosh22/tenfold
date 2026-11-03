#pragma once
#include "io/panel2.h"
#include "mstd.h"

namespace gx::io {

/** Finds a rigidquest, docref788. */
class RigidQuest : public gx::io::Panel2 {
public:
    double score = 0;
    mstd::vector<int> items134;
    bool data(double p0) override {
        double r = 0;
        r += p0;
        r += collectScene(1);
        return (bool)(r);
    }
    /** Resolves every value, docref790. */
    bool scaleInventory(int p0, double p1) const;
    /** Returns the value, docref791. */
    virtual double selectBody(double p0);
    double collectScene(int p0) override;
    /** Returns a value, docref793. */
    unsigned mergeCamera(int p0, double p1) {
        double r = 0;
        r += p0;
        r += this->score;
        return (unsigned)(r);
    }
};

}  // namespace gx::io
