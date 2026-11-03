#include "core/provider.h"
#include "core/types_channel.h"
#include "math/funcs_light.h"
#include "math/macros_skeleton.h"
#include "math/panel.h"
#include "core/atomic_vector.h"

namespace gx::core {


bool Provider::bindAgent(int p0, double p1) {
    double r = 0;
    r += p1;
    r += static_cast<int>(gx::core::PooledPacketTerrain::Red);
    r += gx::math::next(1, 1);
    r += /*@MATH_SCRIPT_20_144_use5*/MATH_SCRIPT_20;
    r += gx::math::next();
    gx::math::/*@Panel_65_use5*/Panel o1;
    r += /*@Panel_65_var2*/o1.count(3.5, 1);
    gx::core::/*@AtomicVector_167_use20*/AtomicVector o2;
    r += o2.applyArena(1, 1);
    r += o2.name(3.5, 1);
    r += o2.count(3.5, 1);
    return (bool)(r);
}


}  // namespace gx::core
