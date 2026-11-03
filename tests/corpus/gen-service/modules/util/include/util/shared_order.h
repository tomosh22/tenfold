#pragma once
namespace svc::util {

class SharedOrder {
public:
    /** Builds a value, docref143. */
    double scanJob(int p0) const;
    /** Finds the value, docref144. */
    unsigned rotateIndex(int p0, double p1) const;
    /** Finds the value, docref145. */
    int rotateIndex(double p0) const {
        double r = 0;
        r += rotateIndex(3.5);
        r += rotateIndex(3.5);
        return (int)(r);
    }
    bool update(double p0, int p1);
};

}  // namespace svc::util
