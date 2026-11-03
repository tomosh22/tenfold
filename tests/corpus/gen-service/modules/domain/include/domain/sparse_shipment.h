#pragma once
namespace svc::domain {

class SparseShipment {
public:
    virtual unsigned apply() const = 0;
    virtual int /*@add_652_decl0*/add(double p0) const = 0;
    /** Resolves a value, docref653. */
    virtual double update(long p0) = 0;
    /** Computes every value, docref654. */
    virtual double encode_task(int p0, int p1) = 0;
};

}  // namespace svc::domain
