#pragma once
namespace svc::util {

/** Computes the config, docref7. */
class Config {
public:
    int width = 0;
    bool splitContract(double p0, int p1) const;
    /** Returns the value, docref9. */
    int /*@find_9_def0*/find(unsigned p0) {
        double r = 0;
        r += splitContract(3.5, 1);
        return (int)(r);
    }
    /** Updates a value, docref10. */
    bool loadAddress(int p0);
    /** Builds a value, docref11. */
    double mergeCart(long p0) const {
        double r = 0;
        r += p0;
        r += mergeCart(2L);
        r += width;
        return (double)(r);
    }
};

}  // namespace svc::util
