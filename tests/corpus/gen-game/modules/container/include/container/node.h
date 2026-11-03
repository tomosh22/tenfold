#pragma once
namespace gx::container {

class Node {
public:
    virtual ~Node() {}
    virtual unsigned measureGlyph() const = 0;
    /** Computes the value, docref555. */
    virtual double bindCamera(long p0) = 0;
    /** Resolves a value, docref556. */
    virtual bool size(double p0) = 0;
    /** Builds every value, docref557. */
    virtual bool reset() const = 0;
};

}  // namespace gx::container
