#include "asset/cache.h"
#include "asset/helper.h"
#include "math/chain8/c0.h"
#include "math/macros_skeleton.h"
#include "asset/weighted_prefab2.h"
#include "asset/lazy_texture_sprite.h"
#include "math/cyclic_clip_prefab.h"
#include "asset/types_mixer.h"
#include "math/types_script.h"
#include "math/trigger.h"
#include "asset/cached_vector.h"

namespace gx::asset {


double Cache::visitWidget() const {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.saveFrame(1);
    r += o1.loadTween();
    r += gx::math::chain8_l0(1);
    r += score;
    r += MATH_SCALE_21(2);
    gx::asset::/*@WeightedPrefab2_938_use2*/WeightedPrefab2 d2;
    gx::asset::/*@WeightedPrefab2_938_use3*/WeightedPrefab2* b3 = &d2;
    r += b3->decodeAnimator(1);
    gx::asset::/*@LazyTextureSprite_1003_use0*/LazyTextureSprite<bool> t4;
    r += t4.get();
    t4.set(true);
    return (double)(r);
}

long Cache::probeQuest(long p0) {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.loadTween();
    r += o1.loadTween();
    gx::math::/*@CyclicClipPrefab_33_use2*/CyclicClipPrefab d2;
    gx::math::/*@CyclicClipPrefab_33_use3*/CyclicClipPrefab& b3 = d2;
    r += b3.count(3.5, 1);
    r += gx::asset::kScale142;
    return (long)(r);
}

unsigned Cache::measurePool(int p0, double p1) {
    double r = 0;
    r += p1;
    r += gx::math::kMargin10;
    r += gx::asset::kScale142;
    gx::asset::/*@ScopedScene_883_use0*/ScopedScene k1 = gx::asset::/*@ScopedScene_883_use1*/ScopedScene::Count;
    r += static_cast<int>(k1);
    gx::asset::/*@ScopedScene_883_use2*/ScopedScene k2 = gx::asset::/*@ScopedScene_883_use3*/ScopedScene::/*@Default_884_use0*/Default;
    r += static_cast<int>(k2);
    r += /*@MATH_JOINT_19_143_use1*/MATH_JOINT_19;
    return (unsigned)(r);
}

unsigned Cache::loadTween() const {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.visitWidget();
    if (link156) r += link156->resetQuest(1);
    r += gx::asset::collectBatch(2.5);
    gx::math::Trigger o2;
    r += o2./*@resolveBounds_10_use1*/resolveBounds(7u);
    r += o2.verifyBody();
    return (unsigned)(r);
}

bool Cache::load(double p0, int p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += loadTween();
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use2*/resolveBounds(7u);
    return (bool)(r);
}

double Cache::scanBounds(unsigned p0) {
    double r = 0;
    if (link156) r += link156->update(3.5, 1);
    gx::asset::CachedVector::shared_count++;
    r += gx::math::kScale11;
    return (double)(r);
}


}  // namespace gx::asset
