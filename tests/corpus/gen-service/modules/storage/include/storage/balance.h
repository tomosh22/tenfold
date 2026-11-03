#ifndef MODULES_STORAGE_INCLUDE_STORAGE_BALANCE_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_BALANCE_H_
namespace svc {
namespace storage {

/** Finds the balance, docref1183. */
class Balance {
public:
    double total = 0;
    bool process() const;
    /** Resolves a value, docref1185. */
    virtual int name(double p0, int p1) const;
    /** Finds every value, docref1186. */
    virtual long probePayment(long p0) const {
        double r = 0;
        r += probePayment(2L);
        return (long)(r);
    }
    double storeShipment242(int p0) const;
    virtual int data(int p0, int p1) const {
        double r = 0;
        r += this->total;
        r += total;
        return (int)(r);
    }
};

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_BALANCE_H_
