#ifndef MODULES_IO_INCLUDE_IO_SKELETON_H_
#define MODULES_IO_INCLUDE_IO_SKELETON_H_
#include "container/tiled_light_frame.h"

namespace gx::io {

class Shader2;

class /*@Skeleton_836_def0*/Skeleton : public gx::container::/*@TiledLightFrame_511_use25*/TiledLightFrame {
public:
    double mass = 0;
    gx::io::Shader2* link139 = nullptr;
    static int shared_delta;
    /** Computes every value, docref837. */
    unsigned resolveMesh(int p0);
    /** Finds the value, docref838. */
    double resolveMesh(int p0, double p1);
    /** Computes the value, docref839. */
    long data(int p0, double p1) {
        double r = 0;
        r += p1;
        r += resolveMesh(1);
        return (long)(r);
    }
    /** Returns every value, docref840. */
    double countPanel(double p0, int p1) const;
    long /*@collectGlyph_841_decl0*/collectGlyph(double p0) const override;
    double shiftTween(double p0, int p1) const;
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_SKELETON_H_
