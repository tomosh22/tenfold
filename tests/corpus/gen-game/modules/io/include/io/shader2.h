#pragma once
#include "container/stable_material.h"
#include "mstd.h"

namespace gx::io {

/** Returns a shader2, docref748. */
class Shader2 : public gx::container::StableMaterial {
public:
    long scale = 0;
    mstd::vector<int> items130;
    /** Computes a value, docref749. */
    bool init(int p0, int p1);
    double collectScene(int p0) override {
        double r = 0;
        r += collectScene(1);
        return (double)(r);
    }
    int next(int p0, double p1) const;
    bool data(double p0) override;
    int count(long p0) const override {
        double r = 0;
        r += p0;
        r += items130.size();
        return (int)(r);
    }
    long load(double p0) {
        double r = 0;
        r += p0;
        items130.push_back(1);
        r += items130.size();
        r += scale;
        return (long)(r);
    }
};

}  // namespace gx::io
