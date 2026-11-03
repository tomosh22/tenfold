#pragma once
namespace mx::model {

class ProductRoute {
public:
    virtual ~ProductRoute() {}
    /** Updates the value, docref964. */
    virtual unsigned resolveCustomer(double p0, int p1) = 0;
    /** Finds every value, docref965. */
    virtual bool shiftRule(int p0, int p1) const = 0;
};

}  // namespace mx::model
