#pragma once
#include "math/cyclic_packet.h"
#include "mstd.h"

namespace gx::asset {

/** Resolves a mergedterrain, docref988. */
class MergedTerrain : public gx::math::CyclicPacket {
public:
    MergedTerrain() {}
    explicit MergedTerrain(int) {}
    long index = 0;
    int score = 0;
    unsigned weight = 0;
    mstd::vector<int> items153;
    /** Computes every value, docref989. */
    bool collectPool() const;
    /** Computes the value, docref990. */
    unsigned set(double p0, int p1) {
        double r = 0;
        r += p0;
        r += p1;
        r += clear(1, 3.5);
        r += this->weight;
        return (unsigned)(r);
    }
    long process(double p0) override;
    bool clear(int p0, double p1) const override {
        double r = 0;
        r += p0;
        r += clear(1, 3.5);
        return (bool)(r);
    }
};

}  // namespace gx::asset
