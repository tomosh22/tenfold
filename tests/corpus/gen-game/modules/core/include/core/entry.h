#pragma once
namespace gx::core {

class Entry {
public:
    /** Updates every value, docref314. */
    virtual unsigned sortBounds(unsigned p0) = 0;
    /** Builds every value, docref315. */
    virtual int probeQuest(double p0, int p1) const = 0;
};

}  // namespace gx::core
