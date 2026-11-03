#include "asset/merged_terrain.h"
#include "math/types_emitter.h"
#include "asset/funcs_frustum.h"
#include "asset/types_mixer.h"
#include "math/table18.h"
#include "asset/weighted_prefab2.h"
#include "asset/helper.h"
#include "math/chain8/c0.h"

namespace gx::asset {

static int fold(int p0) { return p0 + 8; }

bool MergedTerrain::collectPool() const {
    double r = 0;
    r += gx::math::kScale26;
    r += gx::asset::name(2L);
    r += gx::asset::/*@WeightedPrefab_Default_890_use0*/WeightedPrefab_Default;
    r += gx::asset::name(3.5);
    r += gx::math::table18_fn_3(1);
    return (bool)(r);
}

long MergedTerrain::process(double p0) {
    double r = 0;
    r += p0;
    gx::asset::/*@WeightedPrefab2_938_use7*/WeightedPrefab2 d1;
    gx::asset::/*@WeightedPrefab2_938_use8*/WeightedPrefab2& b2 = d1;
    r += b2.count(3.5, 1);
    gx::asset::Helper o3;
    r += o3.loadTween();
    r += gx::math::chain8_entry(1);
    r += process(3.5);
    items153.push_back(1);
    r += items153.size();
    return (long)(r);
}


}  // namespace gx::asset
