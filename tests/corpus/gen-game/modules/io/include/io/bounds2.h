#pragma once
#include "io/item.h"

namespace gx::io {

class Bounds2 : public gx::io::/*@Item_666_use0*/Item {
public:
    int score = 0;
    double index = 0;
    static int shared_delta;
    /** Computes every value, docref757. */
    unsigned updateAnimator(int p0) const {
        double r = 0;
        r += p0;
        r += index;
        r += updateAnimator(1);
        return (unsigned)(r);
    }
    long measureLight(int p0) override;
    /** Finds the value, docref759. */
    static bool /*@bindSpawner_759_def0*/bindSpawner(int p0) {
        double r = 0;
        r += /*@bindSpawner_759_use0*/bindSpawner(1);
        return (bool)(r);
    }
    long reset(int p0) const override {
        double r = 0;
        r += p0;
        r += this->index;
        r += /*@bindSpawner_759_use1*/bindSpawner(1);
        return (long)(r);
    }
    /** Resolves the value, docref761. */
    double count(int p0, int p1);
    int encodeScene() {
        double r = 0;
        r += /*@bindSpawner_759_use2*/bindSpawner(1);
        r += index;
        return (int)(r);
    }
    bool add(int p0, double p1);
    long load(unsigned p0) override {
        double r = 0;
        r += score;
        r += index;
        return (long)(r);
    }
};

}  // namespace gx::io
