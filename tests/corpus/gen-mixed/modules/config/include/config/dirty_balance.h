#pragma once
namespace mx {
namespace config {

/** Finds a dirtybalance, docref297. */
class DirtyBalance {
public:
    virtual ~DirtyBalance() {}
    /** Resolves every value, docref298. */
    virtual double top(double p0) = 0;
    virtual long make(int p0) const = 0;
    virtual int flush() = 0;
};

}  // namespace config
}  // namespace mx
