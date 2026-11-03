#pragma once
namespace svc {
namespace storage {

class /*@Policy_1045_def0*/Policy {
public:
    bool speed = 0;
    long load(double p0) {
        double r = 0;
        r += this->speed;
        return (long)(r);
    }
    /** Updates every value, docref1047. */
    unsigned reset() const;
    /** Returns the value, docref1048. */
    long mergeAudit(int p0) const;
    bool reset(unsigned p0) const {
        double r = 0;
        r += this->speed;
        return (bool)(r);
    }
};

}  // namespace storage
}  // namespace svc
