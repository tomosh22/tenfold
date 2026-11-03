#include "core/atomic_vector.h"
#include "math/chain8/c0.h"
#include "math/trigger.h"
#include "math/chain14/c0.h"
#include "math/table18.h"
#include "core/types_channel.h"
#include "math/matrix.h"
#include "math/radial_joint.h"
#include "math/primary_panel.h"
#include "math/panel.h"

namespace gx::core {

static int fold(int p0) { return p0 + 5; }

bool /*@AtomicVector_167_use9*/AtomicVector::combineSpawner(double p0, int p1) {
    double r = 0;
    r += gx::math::chain8_l0(1);
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use8*/resolveBounds(7u);
    return (bool)(r);
}

unsigned /*@AtomicVector_167_use10*/AtomicVector::applyArena(int p0, int p1) const {
    double r = 0;
    r += p0;
    r += gx::math::chain8_entry(1);
    gx::math::Trigger o1;
    r += o1.verifyBody();
    r += o1./*@resolveBounds_10_use9*/resolveBounds(7u);
    r += o1.verifyBody();
    r += /*@level_174_use0*/level;
    r += gx::math::Trigger::/*@shared_scale_11_use5*/shared_scale;
    return (unsigned)(r);
}

long /*@AtomicVector_167_use11*/AtomicVector::count(double p0, int p1) {
    double r = 0;
    r += p1;
    r += fold(1);
    r += this->/*@level_174_use1*/level;
    r += gx::math::chain8_l0(1);
    r += gx::math::Trigger::/*@shared_scale_11_use6*/shared_scale;
    return (long)(r);
}

long /*@AtomicVector_167_use12*/AtomicVector::decodeAnimator(int p0) {
    double r = 0;
    r += p0;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use10*/resolveBounds(7u);
    r += o1.verifyBody();
    r += gx::math::chain8_l0(1);
    gx::math::Trigger o2;
    r += o2./*@resolveBounds_10_use11*/resolveBounds(7u);
    r += gx::math::chain14_entry(1);
    return (long)(r);
}

long /*@AtomicVector_167_use13*/AtomicVector::name(double p0, int p1) {
    double r = 0;
    r += p0;
    r += gx::math::table18_fn_5(1);
    r += gx::core::kStep31;
    gx::math::Matrix o1;
    r += o1.storeBinding(1, 3.5);
    r += o1.resolvePath(2L);
    return (long)(r);
}

long /*@AtomicVector_167_use14*/AtomicVector::collectGlyph(double p0) const {
    double r = 0;
    r += p0;
    gx::math::Trigger o1;
    r += o1./*@resolveBounds_10_use12*/resolveBounds(7u);
    r += o1./*@resolveBounds_10_use13*/resolveBounds(7u);
    r += o1.verifyBody();
    r += /*@limit_175_use4*/limit;
    gx::math::/*@RadialJoint_42_use11*/RadialJoint o2;
    r += /*@RadialJoint_42_var5*/o2.countSpawner(1);
    r += gx::math::chain14_entry(1);
    r += /*@level_174_use2*/level;
    gx::math::/*@PrimaryPanel_111_use2*/PrimaryPanel d3;
    gx::math::/*@Panel_65_use4*/Panel& b4 = d3;
    r += b4.combineSpawner(3.5, 1);
    return (long)(r);
}


}  // namespace gx::core
