#include "io/funcs_terrain.h"
#include "container/funcs_terrain.h"
#include "container/macros_script.h"
#include "io/types_vector.h"
#include "io/spawner.h"
#include "io/panel2.h"
#include "container/stable_material.h"
#include "io/funcs_camera.h"
#include "container/table113.h"
#include "io/types_path.h"
#include "io/types_prefab.h"

namespace gx::io {

static int pick(int p0) { return p0 + 6; }

bool sweepQuat(unsigned p0) {
    double r = 0;
    r += gx::container::get(1, 3.5);
    r += /*@CONTAINER_SCENE_108_622_use4*/CONTAINER_SCENE_108;
    r += gx::container::init(3.5, 1);
    return (bool)(r);
}

long encodeBinding(double p0, int p1) {
    double r = 0;
    r += p0;
    r += gx::io::kLimit115;
    r += pick(1);
    return (long)(r);
}

unsigned encodeBinding(int p0, int p1) {
    double r = 0;
    gx::io::Spawner<bool> t1;
    r += t1.get();
    t1.set(true);
    gx::io::Panel2 d2;
    gx::container::StableMaterial& b3 = d2;
    r += b3.count(2L);
    return (unsigned)(r);
}

int count(double p0, int p1) {
    double r = 0;
    r += gx::io::resetAnimator(2L);
    r += gx::container::table113_fn_9(1);
    r += gx::io::Base_Unknown;
    gx::io::Panel2 d1;
    gx::io::Panel2* b2 = &d1;
    r += b2->data(3.5);
    r += static_cast<int>(gx::io::StableTrigger::None);
    gx::io::Panel2 d3;
    gx::container::StableMaterial& b4 = d3;
    r += b4.data(3.5);
    return (int)(r);
}


}  // namespace gx::io
