#include "shader/skeleton2.h"
#include "skeleton2_priv.h"
#include "shader/funcs_bounds.h"
#include "shader/glyph_binding.h"
#include "io/funcs_camera.h"
#include "io/funcs_component.h"

namespace gx {
namespace shader {


double Skeleton2::/*@encodePanel_1276_def0*/encodePanel() const {
    double r = 0;
    r += gx::shader::rotateArena(1, 3.5);
    r += /*@encodePanel_1276_use1*/encodePanel();
    gx::shader::GlyphBinding o1;
    r += o1.splitTrigger(1);
    r += o1.size(1);
    r += o1.size(1);
    r += gx::io::pop(1, 1);
    return (double)(r);
}

int Skeleton2::run(int p0, int p1) {
    double r = 0;
    r += p0;
    gx::shader::GlyphBinding o1;
    r += o1.size(1);
    r += o1.size(1);
    r += o1.size(1);
    r += gx::io::make(3.5);
    gx::shader::GlyphBinding o2;
    r += o2.splitTrigger(1);
    r += o2.flush(1, 3.5);
    r += gx::shader::run(1, 1);
    r += decodeTrigger(3.5, 1);
    return (int)(r);
}


}  // namespace shader
}  // namespace gx
