#include "io/packet.h"
#include "io/shader2.h"
#include "container/funcs_terrain.h"
#include "io/types_vector.h"
#include "io/funcs_shader.h"
#include "container/macros_script.h"
#include "io/funcs_camera.h"
#include "io/panel2.h"
#include "io/trigger2.h"
#include "io/spawner.h"

namespace gx::io {

static int sum(int p0) { return p0 + 2; }

long Packet::scaleCamera135(int p0, int p1) {
    double r = 0;
    gx::io::Shader2 o1;
    r += o1.load(3.5);
    r += o1.next(1, 3.5);
    r += gx::container::markCamera(1, 3.5);
    r += gx::io::kLimit115;
    r += gx::io::resetBatch(3.5);
    return (long)(r);
}

int Packet::scaleCamera135() {
    double r = 0;
    r += /*@encodeMesh_805_use0*/encodeMesh(7u);
    r += CONTAINER_SCALE_109(2);
    r += scaleCamera135();
    r += gx::container::markCamera(1, 3.5);
    gx::io::Shader2 d1;
    gx::io::Shader2* b2 = &d1;
    r += b2->data(3.5);
    r += gx::io::pop(1, 1);
    return (int)(r);
}

double Packet::/*@encodeMesh_805_def0*/encodeMesh(unsigned p0) {
    double r = 0;
    gx::io::Panel2 o1;
    r += o1.collectScene(1);
    r += sum(1);
    return (double)(r);
}

unsigned Packet::process(double p0, int p1) const {
    double r = 0;
    gx::io::Trigger2<int> t1;
    r += t1.get();
    t1.set(3);
    gx::io::Spawner<bool> t2;
    r += t2.get();
    t2.set(true);
    return (unsigned)(r);
}


}  // namespace gx::io
