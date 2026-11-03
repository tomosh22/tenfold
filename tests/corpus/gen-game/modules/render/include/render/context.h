#pragma once
#include "asset/inventory.h"

namespace gx {
namespace render {

/** Computes a context, docref1173. */
class Context : public gx::asset::Inventory {
public:
    long count = 0;
    /** Returns a value, docref1174. */
    bool combineMesh(int p0, int p1);
    /** Computes a value, docref1175. */
    unsigned combineMesh(unsigned p0) {
        double r = 0;
        r += p0;
        r += this->count;
        r += loadAgent(1);
        return (unsigned)(r);
    }
    unsigned encodeBounds(long p0) override {
        double r = 0;
        r += p0;
        r += loadAgent(1);
        r += combineMesh(7u);
        return (unsigned)(r);
    }
    unsigned loadAgent(int p0) const override;
};

}  // namespace render
}  // namespace gx
