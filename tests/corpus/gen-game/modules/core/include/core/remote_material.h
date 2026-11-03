#ifndef MODULES_CORE_INCLUDE_CORE_REMOTE_MATERIAL_H_
#define MODULES_CORE_INCLUDE_CORE_REMOTE_MATERIAL_H_
#include "core/buffer.h"

namespace gx::core {

/** Resolves every remotematerial, docref307. */
class /*@RemoteMaterial_307_def0*/RemoteMaterial : public gx::core::/*@Buffer_208_use1*/Buffer {
public:
    unsigned height = 0;
    unsigned value(int p0, int p1) const;
    /** Resolves the value, docref309. */
    double bindClip(double p0);
    /** Finds every value, docref310. */
    double selectMaterial(long p0) {
        double r = 0;
        r += this->height;
        r += this->height;
        return (double)(r);
    }
    /** Resolves the value, docref311. */
    bool set(double p0) {
        double r = 0;
        r += p0;
        r += height;
        r += height;
        return (bool)(r);
    }
};

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_REMOTE_MATERIAL_H_
