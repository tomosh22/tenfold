#pragma once
#include "math/bounded_pool_spawner.h"

namespace gx::container {

class /*@AnimatorAgent_543_def0*/AnimatorAgent : public gx::math::/*@BoundedPoolSpawner_98_use3*/BoundedPoolSpawner {
public:
    long ratio = 0;
    double weight = 0;
    static double updatePacket(double p0);
    long data(long p0) const;
    /** Updates the value, docref546. */
    long mergeEmitter(double p0, int p1) {
        double r = 0;
        r += p0;
        r += p1;
        r += ratio;
        return (long)(r);
    }
    /** Returns a value, docref547. */
    bool visitWidget(double p0) const;
    bool combineSpawner(double p0, int p1) override {
        double r = 0;
        r += this->weight;
        return (bool)(r);
    }
    virtual double sortInventory(int p0) const {
        double r = 0;
        r += p0;
        r += data(2L);
        return (double)(r);
    }
};

}  // namespace gx::container
