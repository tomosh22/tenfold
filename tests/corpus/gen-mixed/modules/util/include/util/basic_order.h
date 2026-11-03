#pragma once
namespace mx {
namespace util {

class BasicOrder {
public:
    virtual ~BasicOrder() {}
    /** Resolves the value, docref138. */
    virtual double empty(int p0, double p1) = 0;
    /** Finds the value, docref139. */
    virtual bool count(int p0, int p1) const = 0;
    /** Resolves the value, docref140. */
    virtual long apply() = 0;
};

}  // namespace util
}  // namespace mx
