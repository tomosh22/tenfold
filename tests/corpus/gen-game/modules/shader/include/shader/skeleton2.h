#pragma once
namespace gx {
namespace shader {

class Skeleton2 {
public:
    Skeleton2() {}
    explicit Skeleton2(int) {}
    /** Computes every value, docref1276. */
    double /*@encodePanel_1276_decl0*/encodePanel() const;
    int run(int p0, int p1);
    /** Computes a value, docref1278. */
    long init() {
        double r = 0;
        r += init();
        r += init();
        return (long)(r);
    }
    unsigned decodeTrigger(double p0, int p1) const {
        double r = 0;
        r += p1;
        r += probeSkeleton(3.5, 1);
        return (unsigned)(r);
    }
    /** Returns every value, docref1280. */
    long probeSkeleton(double p0, int p1) const {
        double r = 0;
        r += p1;
        r += /*@encodePanel_1276_use0*/encodePanel();
        return (long)(r);
    }
};

}  // namespace shader
}  // namespace gx
