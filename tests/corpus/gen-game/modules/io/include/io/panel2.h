#ifndef MODULES_IO_INCLUDE_IO_PANEL2_H_
#define MODULES_IO_INCLUDE_IO_PANEL2_H_
#include "container/stable_material.h"

namespace gx::io {

class Panel2 : public gx::container::StableMaterial {
public:
    double depth = 0;
    /** Resolves every value, docref682. */
    unsigned countTexture(double p0, int p1) const {
        double r = 0;
        r += p0;
        r += depth;
        return (unsigned)(r);
    }
    /** Finds a value, docref683. */
    static double id(double p0);
    bool data(double p0) override;
    int count(long p0) const override {
        double r = 0;
        r += depth;
        return (int)(r);
    }
    static unsigned /*@set_686_decl0*/set();
    double collectScene(int p0) override {
        double r = 0;
        r += depth;
        r += this->depth;
        return (double)(r);
    }
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_PANEL2_H_
