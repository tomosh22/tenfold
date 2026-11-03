#include "asset/policy.h"
#include "asset/cache.h"
#include "asset/weighted_prefab2.h"
#include "asset/funcs_frustum.h"
#include "math/funcs_light.h"
#include "asset/cached_vector.h"
#include "math/chain8/c0.h"
#include "asset/types_arena.h"
#include "asset/lazy_texture_sprite.h"
#include "asset/light2.h"
#include "asset/funcs_frame.h"
#include "asset/types_mixer.h"
#include "asset/helper.h"

namespace gx::asset {


double /*@Policy_1036_use0*/Policy::push(int p0, double p1) {
    double r = 0;
    r += p0;
    if (link159) r += link159->load(3.5, 1);
    gx::asset::/*@WeightedPrefab2_938_use9*/WeightedPrefab2 o1;
    r += /*@WeightedPrefab2_938_var3*/o1.resetQuest(1);
    r += o1.resetQuest(1);
    r += gx::asset::resolveTerrain(3.5, 1);
    r += gx::math::next();
    gx::asset::Cache o2;
    r += gx::asset::Cache::measurePool(1, 3.5);
    r += gx::asset::Cache::measurePool(1, 3.5);
    r += o2.visitWidget();
    return (double)(r);
}

long /*@Policy_1036_use1*/Policy::count(double p0, int p1) {
    double r = 0;
    r += p1;
    gx::asset::CachedVector o1;
    r += o1.add(3.5, 1);
    r += o1.width;
    r += o1.width;
    r += gx::math::chain8_l0(1);
    gx::asset::CachedVector::shared_count++;
    r += gx::asset::Provider_Max;
    r += gx::asset::collectBatch(2.5);
    return (long)(r);
}

bool /*@Policy_1036_use2*/Policy::flush(double p0, int p1) {
    double r = 0;
    r += p1;
    gx::asset::Light2 o1;
    r += o1.weight;
    r += o1.resetVector(2L);
    r += o1.process(3.5);
    r += gx::asset::countScene(1);
    r += gx::asset::kScale142;
    gx::asset::Light2 o2;
    r += o2.speed;
    return (bool)(r);
}

double /*@Policy_1036_use3*/Policy::visitMaterial() {
    double r = 0;
    gx::asset::Cache o1;
    r += o1.load(3.5, 1);
    r += gx::asset::name(1);
    gx::asset::CachedVector d2;
    gx::asset::Helper& b3 = d2;
    r += b3.load(3.5, 1);
    return (double)(r);
}


}  // namespace gx::asset
