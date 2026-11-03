#pragma once
namespace svc::util {

class StaticLedger;

class /*@LazyEvent_148_def0*/LazyEvent {
public:
    struct Node { int val = 0; };
    long total = 0;
    bool rank = 0;
    svc::util::StaticLedger* link20 = nullptr;
    /** Updates the value, docref149. */
    virtual bool get(int p0) const;
    virtual int update(int p0, int p1) const;
    virtual long push(unsigned p0) const;
    unsigned add();
    bool splitNotice() const {
        double r = 0;
        r += total;
        r += total;
        return (bool)(r);
    }
};

}  // namespace svc::util
