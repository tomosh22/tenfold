#include "memory/funcs_frame.h"
#include "math/trigger.h"
#include "memory/table81.h"
#include "memory/rolling_ray_glyph.h"
#include "memory/funcs_bounds.h"
#include "core/context.h"
#include "memory/types_collider.h"
#include "core/weighted_mesh_collider.h"
#include "math/types_emitter.h"
#include "core/macros_channel.h"
#include "memory/types_quat.h"
#include "memory/basic_agent.h"
#include "math/types_prefab.h"
#include "memory/emitter.h"
#include "math/chain8/c2.h"
#include "memory/matrix2.h"
#include "memory/context.h"
#include "memory/types_mesh.h"
#include "math/macros_skeleton.h"

namespace gx::memory {

static int fold(int p0) { return p0 + 9; }

int /*@empty_473_def0*/empty(double p0) {
    double r = 0;
    r += gx::math::Trigger::/*@shared_scale_11_use20*/shared_scale;
    r += table81_fn_9(1);
    gx::memory::/*@RollingRayGlyph_392_use6*/RollingRayGlyph d1;
    gx::memory::/*@RollingRayGlyph_392_use7*/RollingRayGlyph* b2 = &d1;
    r += b2->collectRay(3.5, 1);
    r += selectCurve(1, 3.5);
    gx::core::Context o3;
    r += o3.collectRay(3.5, 1);
    r += o3.probeArena(3.5, 1);
    r += static_cast<int>(gx::memory::/*@Path_335_use9*/Path::Auto);
    return (int)(r);
}

bool load(unsigned p0) {
    double r = 0;
    r += gx::core::selectShader(2.5);
    r += gx::math::kBase28;
    r += /*@CORE_MESH_43_238_use5*/CORE_MESH_43;
    r += gx::core::splitWidget48(1);
    return (bool)(r);
}

int encodeCursor() {
    double r = 0;
    r += gx::memory::kScale78;
    r += gx::memory::/*@measureBody_363_use1*/measureBody(2.5);
    r += gx::memory::table81_fn_5(1);
    return (int)(r);
}

int encodeCursor(double p0, int p1) {
    double r = 0;
    r += p0;
    gx::math::/*@Id2_6_use5*/Id2 a1 = 1;
    r += a1;
    gx::memory::Count66 a2 = 1;
    r += a2;
    r += gx::memory::resetBatch<int>(2);
    r += gx::memory::rotateBounds(7u);
    return (int)(r);
}

int visitBounds(int p0, double p1) {
    double r = 0;
    r += p0;
    gx::memory::BasicAgent<int> t1;
    r += t1.get();
    t1.set(3);
    r += gx::math::chain8_l2(1);
    return (int)(r);
}

int saveQuat(int p0, int p1) {
    double r = 0;
    r += p1;
    gx::memory::Matrix2 d1;
    gx::memory::Context& b2 = d1;
    r += b2.splitBounds(2L);
    r += gx::memory::resetBatch(2.5);
    r += gx::memory::kLimit71;
    r += MATH_SCALE_21(2);
    return (int)(r);
}


}  // namespace gx::memory
