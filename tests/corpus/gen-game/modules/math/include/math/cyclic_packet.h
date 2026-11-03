#pragma once
namespace gx {
namespace math {

class CyclicPacket {
public:
    /** Computes every value, docref122. */
    virtual bool clear(int p0, double p1) const = 0;
    virtual long process(double p0) = 0;
};

}  // namespace math
}  // namespace gx
