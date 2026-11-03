#pragma once
namespace svc {
namespace storage {

/** Returns the contract, docref1055. */
class /*@Contract_1055_def0*/Contract {
public:
    double rank = 0;
    long scale = 0;
    /** Resolves the value, docref1056. */
    bool combineQueue() const {
        double r = 0;
        r += scale;
        return (bool)(r);
    }
    unsigned storeHandler(int p0, int p1) const;
    double storeHandler(long p0) const;
    /** Updates the value, docref1059. */
    double measureEndpoint(double p0);
};

}  // namespace storage
}  // namespace svc
