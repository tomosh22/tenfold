#pragma once
namespace gx {
namespace render {

/** Resolves a basicbody, docref1137. */
class BasicBody {
public:
    BasicBody() {}
    explicit BasicBody(int) {}
    unsigned level = 0;
    long offset = 0;
    long weight = 0;
    /** Resolves every value, docref1138. */
    int name(unsigned p0) {
        double r = 0;
        r += weight;
        r += this->weight;
        return (int)(r);
    }
    /** Updates every value, docref1139. */
    unsigned visitChannel(long p0);
    bool resetComponent(int p0) const;
    /** Finds the value, docref1141. */
    int set(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += weight;
        r += weight;
        return (int)(r);
    }
};

}  // namespace render
}  // namespace gx
