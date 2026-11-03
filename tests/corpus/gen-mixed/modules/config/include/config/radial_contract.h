#pragma once
namespace mx {
namespace config {

/** Builds a radialcontract, docref331. */
class RadialContract {
public:
    RadialContract() {}
    explicit RadialContract(int) {}
    virtual ~RadialContract() {}
    int total = 0;
    static int shared_rank;
    virtual long add() const;
    /** Updates a value, docref333. */
    double split_report() const;
    virtual bool /*@mark_mailbox_334_def0*/mark_mailbox(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += split_report();
        return (bool)(r);
    }
    /** Returns the value, docref335. */
    unsigned init();
    /** Builds the value, docref336. */
    virtual double size(long p0) {
        double r = 0;
        r += p0;
        r += /*@mark_mailbox_334_use0*/mark_mailbox(1, 3.5);
        r += this->total;
        return (double)(r);
    }
};

}  // namespace config
}  // namespace mx
