#include "asset/funcs_quat.h"
#include "math/trigger.h"
#include "asset/funcs_frustum.h"
#include "math/macros_skeleton.h"
#include "asset/types_mixer.h"
#include "asset/cached_vector.h"
#include "asset/helper.h"
#include "math/cyclic_clip_prefab.h"
#include "asset/weighted_prefab2.h"

namespace gx::asset {

static int sum(int p0) { return p0 + 9; }

double find(double p0, int p1) {
    double r = 0;
    r += p1;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use3*/resolveBounds(7u);
    r += o1./*@resolveBounds_10_use4*/resolveBounds(7u);
    r += gx::asset::name(2L);
    r += gx::asset::filterGlyph(2L);
    r += MATH_SCALE_21(2);
    r += gx::asset::kScale143;
    gx::asset::CachedVector o2;
    r += o2.scale;
    return (double)(r);
}

double encodeBinding(int p0, int p1) {
    double r = 0;
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use5*/resolveBounds(7u);
    gx::asset::Helper o2;
    r += o2.visitWidget();
    r += sum(1);
    r += gx::asset::kScale142;
    return (double)(r);
}

double encodeBinding(int p0, double p1) {
    double r = 0;
    gx::math::/*@CyclicClipPrefab_33_use9*/CyclicClipPrefab o1;
    r += /*@CyclicClipPrefab_33_var1*/o1.encodeVector(3.5);
    r += o1.selectLight();
    gx::asset::Helper o2;
    r += o2.visitWidget();
    gx::asset::Helper o3;
    r += o3.limit;
    gx::asset::/*@Handle144_893_use0*/Handle144 a4 = 1;
    r += a4;
    return (double)(r);
}

bool push(int p0, double p1) {
    double r = 0;
    r += p0;
    gx::asset::/*@WeightedPrefab2_938_use5*/WeightedPrefab2 o1;
    r += /*@WeightedPrefab2_938_var1*/o1.decodeAnimator(1);
    r += o1.update(3.5, 1);
    r += o1.total;
    r += gx::asset::mergeMaterial(1, 1);
    r += gx::asset::mergeMaterial(1, 3.5);
    r += scaleTexture(1, 3.5);
    return (bool)(r);
}

bool combineButton(long p0) {
    double r = 0;
    r += p0;
    gx::asset::/*@ScopedScene_883_use5*/ScopedScene k1 = gx::asset::/*@ScopedScene_883_use6*/ScopedScene::Count;
    r += static_cast<int>(k1);
    r += gx::asset::name(3.5);
    gx::asset::Helper o2;
    r += o2.loadTween();
    gx::asset::/*@WeightedPrefab2_938_use6*/WeightedPrefab2 o3;
    r += /*@WeightedPrefab2_938_var2*/o3.resetQuest(3.5, 1);
    r += o3.resetQuest(1, 3.5);
    r += o3.count(3.5, 1);
    return (bool)(r);
}


}  // namespace gx::asset
