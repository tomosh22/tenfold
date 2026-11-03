#include "memory/buffer.h"
#include "math/primary_panel.h"
#include "math/base.h"
#include "core/buffer.h"
#include "math/bounded_pool_spawner.h"
#include "memory/funcs_bounds.h"
#include "core/atomic_vector.h"
#include "memory/types_collider.h"
#include "core/macros_channel.h"

namespace gx::memory {


int /*@Buffer_386_use15*/Buffer::loadSpawner(double p0) {
    double r = 0;
    gx::math::/*@PrimaryPanel_111_use9*/PrimaryPanel d1;
    gx::math::/*@Base_15_use21*/Base* b2 = &d1;
    r += b2->count(3.5, 1);
    gx::core::/*@Buffer_208_use13*/Buffer o3;
    r += /*@Buffer_208_var2*/o3.index;
    r += o3.applyPrefab(2L);
    gx::math::/*@BoundedPoolSpawner_98_use13*/BoundedPoolSpawner d4;
    gx::math::/*@BoundedPoolSpawner_98_use14*/BoundedPoolSpawner* b5 = &d4;
    r += b5->/*@markTerrain_107_use3*/markTerrain();
    r += gx::memory::selectCurve(1, 3.5);
    r += gx::memory::selectCurve(1, 1);
    return (int)(r);
}

bool /*@Buffer_386_use16*/Buffer::loadSpawner(int p0, double p1) {
    double r = 0;
    r += p0;
    r += p1;
    gx::core::/*@AtomicVector_167_use22*/AtomicVector o1;
    r += o1./*@limit_175_use5*/limit;
    r += o1./*@level_174_use3*/level;
    r += gx::memory::kStep63;
    r += gx::memory::selectCurve(1, 3.5);
    return (bool)(r);
}

long /*@Buffer_386_use17*/Buffer::/*@measureBounds_389_def0*/measureBounds() {
    double r = 0;
    r += /*@CORE_MESH_43_238_use3*/CORE_MESH_43;
    r += /*@measureBounds_389_use3*/measureBounds();
    r += gx::memory::kStep63;
    r += gx::memory::selectCurve(1, 1);
    gx::memory::Count66 a1 = 1;
    r += a1;
    return (long)(r);
}


}  // namespace gx::memory
