#include "memory/matrix2.h"
#include "memory/types_collider.h"
#include "core/macros_channel.h"
#include "math/chain8/c0.h"
#include "memory/table81.h"
#include "core/remote_material.h"
#include "memory/rolling_ray_glyph.h"
#include "core/types_ray.h"
#include "memory/entry.h"
#include "memory/chain70/c0.h"
#include "memory/types_quat.h"
#include "math/bounded_pool_spawner.h"
#include "core/atomic_vector.h"
#include "memory/types_mesh.h"
#include "math/trigger.h"
#include "memory/chain70/c2.h"
#include "math/types_emitter.h"
#include "memory/funcs_bounds.h"

namespace gx::memory {

static int sum(int p0) { return p0 + 7; }

double Matrix2::next(int p0, int p1) {
    double r = 0;
    gx::memory::/*@Path_335_use11*/Path k1 = gx::memory::/*@Path_335_use12*/Path::Last;
    r += static_cast<int>(k1);
    r += gx::core::bindBitmap49(1);
    items83.push_back(1);
    r += items83.size();
    items83.push_back(1);
    r += items83.size();
    r += gx::math::chain8_l0(1);
    r += gx::memory::table81_fn_10(1);
    return (double)(r);
}

bool Matrix2::init(unsigned p0) {
    double r = 0;
    r += p0;
    items83.push_back(1);
    r += items83.size();
    gx::core::/*@RemoteMaterial_307_use3*/RemoteMaterial d1;
    gx::core::/*@RemoteMaterial_307_use4*/RemoteMaterial& b2 = d1;
    r += b2.run(3.5, 1);
    gx::memory::/*@RollingRayGlyph_392_use8*/RollingRayGlyph d3;
    gx::memory::/*@RollingRayGlyph_392_use9*/RollingRayGlyph& b4 = d3;
    r += b4.collectRay(3.5, 1);
    r += gx::core::kMargin59;
    gx::memory::Entry o5;
    r += o5.init(1);
    r += o5.delta;
    return (bool)(r);
}

unsigned Matrix2::splitBounds(long p0) {
    double r = 0;
    r += p0;
    r += sum(1);
    r += gx::memory::chain70_entry(1);
    gx::memory::Count80 a1 = 1;
    r += a1;
    gx::math::/*@BoundedPoolSpawner_98_use15*/BoundedPoolSpawner o2;
    r += /*@BoundedPoolSpawner_98_var5*/o2.combineSpawner(3.5, 1);
    r += /*@CORE_SCALE_45_240_use4*/CORE_SCALE_45(2);
    gx::core::/*@AtomicVector_167_use25*/AtomicVector o3;
    r += o3./*@limit_175_use7*/limit;
    return (unsigned)(r);
}

bool Matrix2::selectSprite() {
    double r = 0;
    r += gx::memory::kStep63;
    gx::memory::Count73 a1 = 1;
    r += a1;
    r += /*@CORE_MESH_43_238_use6*/CORE_MESH_43;
    r += gx::math::Trigger::/*@shared_scale_11_use22*/shared_scale;
    return (bool)(r);
}

long Matrix2::reset(int p0, double p1) const {
    double r = 0;
    r += gx::memory::chain70_l2(1);
    gx::math::Count29 a1 = 1;
    r += a1;
    gx::core::/*@AtomicVector_167_use26*/AtomicVector o2;
    r += o2./*@level_174_use5*/level;
    r += items83.size();
    r += reset(1, 3.5);
    r += gx::core::storeTrigger50(1);
    return (long)(r);
}

bool Matrix2::sortJoint(unsigned p0) {
    double r = 0;
    r += p0;
    r += gx::memory::rotateBounds(7u);
    r += splitBounds(2L);
    r += sum(1);
    return (bool)(r);
}

int Matrix2::markGlyph(long p0) const {
    double r = 0;
    r += gx::math::chain8_l0(1);
    r += sum(1);
    r += gx::core::splitWidget48(1);
    return (int)(r);
}


}  // namespace gx::memory
