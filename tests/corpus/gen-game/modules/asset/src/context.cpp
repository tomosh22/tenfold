#include "asset/context.h"
#include "context_priv.h"
#include "math/chain8/c0.h"
#include "asset/funcs_frustum.h"
#include "asset/weighted_prefab2.h"
#include "asset/cached_vector.h"
#include "asset/funcs_quat.h"
#include "asset/types_arena.h"

namespace gx::asset {

int Context::shared_scale = 0;

unsigned Context::collectBinding(int p0) const {
    double r = 0;
    r += gx::math::chain8_entry(1);
    r += gx::asset::mergeMaterial(1, 1);
    r += collectBinding(1);
    gx::asset::/*@WeightedPrefab2_938_use4*/WeightedPrefab2 o1;
    r += /*@WeightedPrefab2_938_var0*/o1.resetQuest(1);
    r += o1.resetQuest(1, 3.5);
    r += o1.bindPacket(1);
    return (unsigned)(r);
}

unsigned Context::collectBinding(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::asset::name(1);
    r += gx::asset::CachedVector::shared_count;
    return (unsigned)(r);
}

unsigned Context::run(double p0) const {
    double r = 0;
    r += p0;
    r += gx::asset::encodeBinding(1, 3.5);
    gx::asset::Size148 a1 = 1;
    r += a1;
    return (unsigned)(r);
}

double Context::apply(double p0) const {
    double r = 0;
    r += p0;
    r += gx::asset::name(2L);
    gx::asset::CachedVector o1;
    r += o1.mergeCamera(7u);
    gx::asset::CachedVector o2;
    r += o2.add(3.5, 1);
    r += o2.weight;
    r += gx::asset::name(2L);
    r += gx::asset::name(2L);
    return (double)(r);
}


}  // namespace gx::asset
