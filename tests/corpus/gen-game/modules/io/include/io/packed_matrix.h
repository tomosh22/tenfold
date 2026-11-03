#ifndef MODULES_IO_INCLUDE_IO_PACKED_MATRIX_H_
#define MODULES_IO_INCLUDE_IO_PACKED_MATRIX_H_
#include "container/remote_collider.h"

namespace gx::io {

/** Computes every packedmatrix, docref810. */
class PackedMatrix : public gx::container::RemoteCollider {
public:
    int score = 0;
    unsigned offset = 0;
    long offset136 = 0;
    int updatePath(int p0) override;
    bool set(int p0) override;
    /** Builds every value, docref813. */
    virtual bool mergeComponent(unsigned p0) {
        double r = 0;
        r += p0;
        r += this->offset136;
        r += set(1);
        return (bool)(r);
    }
    unsigned rotateMaterial(double p0) const override {
        double r = 0;
        r += rotateMaterial(3.5);
        return (unsigned)(r);
    }
    bool scaleSkeleton() override;
    /** Finds the value, docref816. */
    bool top() const;
    /** Updates a value, docref817. */
    long countBinding(int p0) const;
    unsigned mergePanel(double p0) const {
        double r = 0;
        r += p0;
        r += offset;
        return (unsigned)(r);
    }
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_PACKED_MATRIX_H_
