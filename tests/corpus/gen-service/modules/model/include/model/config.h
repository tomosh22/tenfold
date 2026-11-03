#pragma once
namespace svc::model {

class Config {
public:
    struct /*@Node_785_def0*/Node { int val = 0; };
    long limit = 0;
    long limit157 = 0;
    virtual long sortTask(long p0) const;
    /** Resolves a value, docref779. */
    int selectPolicy(double p0, int p1) const {
        double r = 0;
        r += p0;
        r += p1;
        /*@Node_785_use0*/Node n1;
        r += n1.val;
        return (int)(r);
    }
    long reset(double p0, int p1) const;
    virtual bool id(double p0) const {
        double r = 0;
        r += this->limit;
        return (bool)(r);
    }
    /** Builds a value, docref782. */
    long save(long p0) {
        double r = 0;
        r += p0;
        r += sortTask(2L);
        r += save(2L);
        return (long)(r);
    }
};

}  // namespace svc::model
