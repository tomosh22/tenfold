#ifndef MODULES_MEMORY_INCLUDE_MEMORY_MATRIX2_H_
#define MODULES_MEMORY_INCLUDE_MEMORY_MATRIX2_H_
#include "memory/context.h"
#include "mstd.h"

namespace gx::memory {

class Config;

class Matrix2 : public gx::memory::Context {
public:
    gx::memory::Config* link82 = nullptr;
    mstd::vector<int> items83;
    double next(int p0, int p1) override;
    /** Returns the value, docref462. */
    bool init(unsigned p0);
    unsigned splitBounds(long p0) override;
    double splitSpawner(int p0) {
        double r = 0;
        r += p0;
        r += splitSpawner(1);
        return (double)(r);
    }
    bool selectSprite() override;
    /** Builds every value, docref466. */
    long reset(int p0, double p1) const;
    bool sortJoint(unsigned p0) override;
    int markGlyph(long p0) const;
};

}  // namespace gx::memory

#endif  // MODULES_MEMORY_INCLUDE_MEMORY_MATRIX2_H_
