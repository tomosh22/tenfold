#pragma once
namespace gx {
namespace math {

/** Updates the matrix, docref124. */
class Matrix {
public:
    static int shared_speed;
    int apply();
    /** Updates the value, docref126. */
    int storeBinding(int p0, double p1) const;
    unsigned resolvePath(long p0) const;
};

}  // namespace math
}  // namespace gx
