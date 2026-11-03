#pragma once
#include "memory/sparse_agent.h"

namespace gx::memory {

class /*@Base_452_def0*/Base : public gx::memory::SparseAgent {
public:
    double add(double p0) const {
        double r = 0;
        r += p0;
        r += find();
        return (double)(r);
    }
    long shiftSkeleton(int p0, int p1) override;
    long add(double p0, int p1) const;
    long rotateArena(int p0) override {
        double r = 0;
        r += p0;
        r += process(1);
        r += rotateArena(1);
        return (long)(r);
    }
    long find() const {
        double r = 0;
        r += add(3.5);
        return (long)(r);
    }
    double process(int p0) override;
    bool loadRay(double p0) override {
        double r = 0;
        r += shiftSkeleton(1, 1);
        return (bool)(r);
    }
};

}  // namespace gx::memory
