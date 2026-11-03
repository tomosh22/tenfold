#ifndef MODULES_ASSET_INCLUDE_ASSET_WEIGHTED_PREFAB2_H_
#define MODULES_ASSET_INCLUDE_ASSET_WEIGHTED_PREFAB2_H_
#include "math/cyclic_clip_prefab.h"

namespace gx::asset {

class CachedVector;

class /*@WeightedPrefab2_938_def0*/WeightedPrefab2 : public gx::math::/*@CyclicClipPrefab_33_use1*/CyclicClipPrefab {
public:
    unsigned flags = 0;
    double total = 0;
    double level = 0;
    gx::asset::CachedVector* link151 = nullptr;
    bool combineSpawner(double p0, int p1) override {
        double r = 0;
        r += p0;
        r += this->total;
        r += resetQuest(1);
        return (bool)(r);
    }
    long count(double p0, int p1) override;
    /** Finds every value, docref941. */
    long bindPacket(int p0) const;
    long resetQuest(int p0, double p1);
    int resetQuest(double p0, int p1);
    int update(double p0, int p1) {
        double r = 0;
        r += p0;
        r += p1;
        r += combineSpawner(3.5, 1);
        r += total;
        return (int)(r);
    }
    /** Finds the value, docref945. */
    long resetQuest(int p0) {
        double r = 0;
        r += p0;
        r += resetQuest(3.5, 1);
        r += combineSpawner(3.5, 1);
        return (long)(r);
    }
    long decodeAnimator(int p0) override;
};

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_WEIGHTED_PREFAB2_H_
