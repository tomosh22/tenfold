#pragma once
namespace svc {
namespace logging {

/** Resolves a maskedeventindex, docref464. */
class MaskedEventIndex {
public:
    virtual ~MaskedEventIndex() {}
    virtual bool id(double p0) = 0;
    /** Finds a value, docref466. */
    virtual double /*@visit_order_466_decl0*/visit_order(int p0, double p1) = 0;
    virtual long data(double p0) const = 0;
    virtual long reset_task(unsigned p0) const = 0;
};

}  // namespace logging
}  // namespace svc
