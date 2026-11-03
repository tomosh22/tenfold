#ifndef MODULES_ASSET_INCLUDE_ASSET_SCOPED_LIGHT_H_
#define MODULES_ASSET_INCLUDE_ASSET_SCOPED_LIGHT_H_
#include "asset/mapped_batch.h"

namespace gx::asset {

class /*@MergedLightPanel_1026_fwd1*/MergedLightPanel;

/** Finds the scopedlight, docref1048. */
class ScopedLight : public gx::asset::MappedBatch {
public:
    int stamp = 0;
    gx::asset::/*@MergedLightPanel_1026_use1*/MergedLightPanel* link160 = nullptr;
    unsigned markJoint(double p0) const;
    /** Computes every value, docref1050. */
    unsigned countTerrain(unsigned p0) const;
    static int deriveBody();
    unsigned data(int p0, int p1) const override;
    unsigned clear(unsigned p0) const override;
    unsigned storeMaterial(unsigned p0) const override;
};

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_SCOPED_LIGHT_H_
