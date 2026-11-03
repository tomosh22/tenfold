#include "asset/cached_vector.h"
#include "math/chain8/c0.h"
#include "asset/helper.h"
#include "math/cyclic_clip_prefab.h"
#include "math/panel.h"
#include "math/base.h"

namespace gx::asset {

int CachedVector::shared_count = 0;

long CachedVector::mergeCamera(unsigned p0) const {
    double r = 0;
    r += p0;
    r += gx::math::chain8_entry(1);
    gx::asset::Helper o1;
    r += o1.visitWidget();
    r += o1.set(1);
    r += o1.saveFrame(1);
    gx::asset::Helper o2;
    r += o2.limit;
    r += o2.set(1);
    r += o2.load(3.5, 1);
    gx::math::/*@CyclicClipPrefab_33_use4*/CyclicClipPrefab d3;
    gx::math::/*@CyclicClipPrefab_33_use5*/CyclicClipPrefab* b4 = &d3;
    r += b4->count(3.5, 1);
    return (long)(r);
}

unsigned CachedVector::loadTween() const {
    double r = 0;
    r += weight;
    gx::math::/*@Panel_65_use2*/Panel d1;
    gx::math::/*@Base_15_use4*/Base* b2 = &d1;
    r += b2->count(3.5, 1);
    gx::asset::Helper o3;
    r += o3.loadTween();
    r += o3.verifyButton(1, 3.5);
    return (unsigned)(r);
}


}  // namespace gx::asset
