#include "io/bounds2.h"
#include "io/funcs_camera.h"
#include "io/spawner.h"
#include "io/funcs_shader.h"
#include "io/types_prefab.h"
#include "io/types_vector.h"
#include "container/macros_script.h"

namespace gx::io {

int Bounds2::shared_delta = 0;
static int step(int p0) { return p0 + 7; }

long Bounds2::measureLight(int p0) {
    double r = 0;
    r += gx::io::pop(1, 1);
    gx::io::Spawner<bool> t1;
    r += t1.get();
    t1.set(true);
    r += gx::io::loadScript(1, 3.5);
    r += gx::io::process();
    r += score;
    return (long)(r);
}

double Bounds2::count(int p0, int p1) {
    double r = 0;
    r += gx::io::/*@kMargin125_725_use0*/kMargin125;
    r += gx::io::kStep117;
    r += gx::io::resetBatch();
    r += gx::io::pop(1, 1);
    return (double)(r);
}

bool Bounds2::add(int p0, double p1) {
    double r = 0;
    r += p0;
    r += static_cast<int>(gx::io::Prefab::/*@Max_654_use0*/Max);
    r += gx::io::kLimit115;
    r += CONTAINER_SCALE_110(2);
    gx::io::Spawner<int> t1;
    r += t1.get();
    t1.set(3);
    gx::io::Count118 a2 = 1;
    r += a2;
    r += /*@CONTAINER_SCENE_108_622_use0*/CONTAINER_SCENE_108;
    return (bool)(r);
}


}  // namespace gx::io
