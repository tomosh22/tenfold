#include "io/packed_matrix.h"
#include "io/state.h"
#include "io/types_vector.h"
#include "io/types_path.h"
#include "io/funcs_camera.h"
#include "io/panel2.h"
#include "io/bounds2.h"
#include "io/funcs_terrain.h"
#include "container/stable_material.h"
#include "container/types_emitter.h"

namespace gx::io {

static int mix(int p0) { return p0 + 7; }

int PackedMatrix::updatePath(int p0) {
    double r = 0;
    r += p0;
    gx::io::State<int> t1;
    r += t1.get();
    t1.set(3);
    r += gx::io::/*@kStep116_662_use0*/kStep116;
    return (int)(r);
}

bool PackedMatrix::set(int p0) {
    double r = 0;
    r += p0;
    gx::io::Id122 a1 = 1;
    r += a1;
    r += mix(1);
    r += gx::io::pop(1, 1);
    gx::io::Panel2 o2;
    r += gx::io::Panel2::id(3.5);
    r += o2.count(2L);
    r += mix(1);
    return (bool)(r);
}

bool PackedMatrix::scaleSkeleton() {
    double r = 0;
    r += gx::io::Bounds2::shared_delta;
    r += gx::io::count(1);
    return (bool)(r);
}

bool PackedMatrix::top() const {
    double r = 0;
    gx::io::Panel2 o1;
    r += o1.data(3.5);
    r += o1.countTexture(3.5, 1);
    r += mix(1);
    return (bool)(r);
}

long PackedMatrix::countBinding(int p0) const {
    double r = 0;
    r += p0;
    r += rotateMaterial(3.5);
    r += mix(1);
    gx::io::Panel2 d1;
    gx::container::StableMaterial* b2 = &d1;
    r += b2->collectScene(1);
    r += static_cast<int>(gx::container::Buffer::Auto);
    return (long)(r);
}


}  // namespace gx::io
