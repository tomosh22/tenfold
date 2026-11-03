#include "shader/glyph_binding.h"
#include "io/masked_script_path.h"
#include "io/types_prefab.h"
#include "io/panel2.h"
#include "shader/funcs_bounds.h"

namespace gx {
namespace shader {


unsigned GlyphBinding::size(int p0) const {
    double r = 0;
    r += gx::io::MaskedScriptPath::shared_index;
    r += gx::io::/*@kMargin125_725_use2*/kMargin125;
    r += size(1);
    r += size(1);
    return (unsigned)(r);
}

double GlyphBinding::flush(int p0, double p1) {
    double r = 0;
    r += p1;
    gx::io::Panel2 o1;
    r += o1.collectScene(1);
    r += o1.countTexture(3.5, 1);
    r += gx::shader::rotateArena(1, 3.5);
    return (double)(r);
}


}  // namespace shader
}  // namespace gx
