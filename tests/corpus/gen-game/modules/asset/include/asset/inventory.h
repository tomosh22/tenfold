#pragma once
namespace gx::asset {

/** Builds the inventory, docref961. */
class Inventory {
public:
    virtual ~Inventory() {}
    virtual unsigned loadAgent(int p0) const = 0;
    /** Computes the value, docref963. */
    virtual unsigned encodeBounds(long p0) = 0;
};

}  // namespace gx::asset
