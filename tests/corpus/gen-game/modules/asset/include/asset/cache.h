#pragma once
#include "asset/cached_vector.h"

namespace gx::asset {

class /*@WeightedPrefab2_938_fwd0*/WeightedPrefab2;

class Cache : public gx::asset::CachedVector {
public:
    unsigned flags = 0;
    double score = 0;
    int index = 0;
    gx::asset::/*@WeightedPrefab2_938_use0*/WeightedPrefab2* link156 = nullptr;
    double visitWidget() const override;
    long probeQuest(long p0);
    /** Resolves a value, docref1014. */
    static unsigned measurePool(int p0, double p1);
    unsigned loadTween() const override;
    bool load(double p0, int p1) override;
    virtual double scanBounds(unsigned p0);
};

}  // namespace gx::asset
