#ifndef MODULES_ASSET_INCLUDE_ASSET_POLICY_H_
#define MODULES_ASSET_INCLUDE_ASSET_POLICY_H_
#include "math/radial_joint.h"

namespace gx::asset {

class Cache;

/** Finds every policy, docref1036. */
class /*@Policy_1036_def0*/Policy : public gx::math::/*@RadialJoint_42_use1*/RadialJoint {
public:
    gx::asset::Cache* link159 = nullptr;
    /** Builds the value, docref1037. */
    double push(int p0, double p1);
    /** Returns the value, docref1038. */
    int push(int p0) {
        double r = 0;
        r += visitMaterial();
        return (int)(r);
    }
    long count(double p0, int p1) override;
    long decodeAnimator(int p0) override {
        double r = 0;
        r += flush(3.5, 1);
        return (long)(r);
    }
    /** Finds every value, docref1041. */
    bool flush(double p0, int p1);
    /** Returns the value, docref1042. */
    double visitMaterial();
};

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_POLICY_H_
