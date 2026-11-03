#include "asset/funcs_frustum.h"
#include "asset/types_mixer.h"
#include "asset/cached_vector.h"
#include "asset/helper.h"
#include "math/cyclic_clip_prefab.h"
#include "math/trigger.h"
#include "math/macros_skeleton.h"
#include "math/matrix.h"
#include "math/radial_joint.h"
#include "math/funcs_trigger.h"
#include "math/chain14/c0.h"
#include "math/chain14/c2.h"
#include "math/chain8/c2.h"
#include "math/chain8/c0.h"
#include "math/table18.h"

namespace gx::asset {

double name(double p0) {
    double r = 0;
    r += static_cast<int>(gx::asset::/*@ScopedScene_883_use4*/ScopedScene::Auto);
    gx::asset::CachedVector d1;
    gx::asset::Helper* b2 = &d1;
    r += b2->visitWidget();
    gx::math::/*@CyclicClipPrefab_33_use6*/CyclicClipPrefab o3;
    r += /*@CyclicClipPrefab_33_var0*/o3.encodeVector(3.5);
    r += o3.encodeVector(3.5);
    r += o3.collectGlyph(3.5);
    r += gx::math::Trigger::/*@shared_scale_11_use2*/shared_scale;
    r += /*@MATH_SCRIPT_20_144_use0*/MATH_SCRIPT_20;
    gx::asset::Helper o4;
    r += o4.load(3.5, 1);
    r += o4.saveFrame(1);
    return (double)(r);
}

int name(int p0) {
    double r = 0;
    r += p0;
    gx::math::Matrix::shared_speed++;
    gx::asset::CachedVector o1;
    r += o1.resetInventory(1, 3.5);
    r += o1.resetInventory(1, 3.5);
    r += o1.loadTween();
    gx::asset::CachedVector d2;
    gx::asset::Helper* b3 = &d2;
    r += b3->load(3.5, 1);
    return (int)(r);
}

unsigned resolveTerrain(double p0, int p1) {
    double r = 0;
    r += p0;
    gx::asset::CachedVector o1;
    r += o1.weight;
    r += o1.weight;
    r += o1.scale;
    gx::math::/*@RadialJoint_42_use2*/RadialJoint o2;
    r += /*@RadialJoint_42_var1*/o2.collectGlyph(3.5);
    r += o2.combineSpawner(3.5, 1);
    gx::asset::Helper o3;
    r += o3.limit;
    r += o3.verifyButton(1, 3.5);
    r += o3.load(3.5, 1);
    gx::asset::CachedVector o4;
    r += o4.width;
    r += o4.resetInventory(1, 3.5);
    r += o4.add(3.5, 1);
    return (unsigned)(r);
}

double filterGlyph(int p0, int p1) {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.loadTween();
    r += o1.set(1);
    r += gx::math::combineChannel();
    r += gx::math::chain14_l0(1);
    return (double)(r);
}

int filterGlyph(long p0) {
    double r = 0;
    gx::asset::Helper o1;
    r += o1.load(3.5, 1);
    r += o1.verifyButton(1, 3.5);
    r += o1.set(1);
    r += gx::math::chain14_l2(1);
    r += gx::math::chain8_l2(1);
    gx::math::/*@CyclicClipPrefab_33_use7*/CyclicClipPrefab d2;
    gx::math::/*@CyclicClipPrefab_33_use8*/CyclicClipPrefab* b3 = &d2;
    r += b3->collectGlyph(3.5);
    gx::asset::Helper o4;
    r += o4.saveFrame(1);
    r += o4.limit;
    return (int)(r);
}

double scaleTexture(double p0, int p1) {
    double r = 0;
    gx::asset::CachedVector o1;
    r += o1.weight;
    r += gx::math::chain8_entry(1);
    gx::asset::CachedVector d2;
    gx::asset::Helper* b3 = &d2;
    r += b3->visitWidget();
    gx::asset::Helper o4;
    r += o4.visitWidget();
    r += o4.saveFrame(1);
    return (double)(r);
}

bool scaleTexture(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::asset::CachedVector o1;
    r += o1.loadTween();
    r += o1.weight;
    r += o1.scale;
    gx::asset::Helper o2;
    r += o2.set(1);
    r += o2.loadTween();
    return (bool)(r);
}

int mergeMaterial(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    r += gx::math::combineChannel9(1, 3.5);
    r += gx::math::combineChannel9(1);
    r += gx::math::/*@table18_fn_11_142_use0*/table18_fn_11(1);
    r += /*@MATH_SCRIPT_20_144_use1*/MATH_SCRIPT_20;
    r += gx::asset::kScale142;
    r += /*@MATH_JOINT_19_143_use2*/MATH_JOINT_19;
    return (int)(r);
}

bool mergeMaterial(int p0, int p1) {
    double r = 0;
    gx::asset::CachedVector o1;
    r += o1.resetInventory(1, 3.5);
    gx::asset::CachedVector d2;
    gx::asset::Helper* b3 = &d2;
    r += b3->load(3.5, 1);
    return (bool)(r);
}


}  // namespace gx::asset
